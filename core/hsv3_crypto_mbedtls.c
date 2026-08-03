/* Backend mbedTLS das primitivas do HELTEC-SEED-V3.
 *
 * O mesmo ficheiro serve o build de teste no PC e o firmware no ESP32-S3,
 * porque o ESP-IDF ja traz o mbedTLS (com aceleracao de hardware para SHA no
 * S3). Host e firmware partilharem o backend e o que garante que os testes no
 * PC dizem alguma coisa sobre o que corre na placa.
 */

#include "hsv3_crypto.h"

#include <string.h>

#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/ripemd160.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecp.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/constant_time.h"

void hsv3_sha256(const uint8_t *in, size_t len, uint8_t out[32])
{
    (void)mbedtls_sha256(in, len, out, 0);
}

void hsv3_sha512(const uint8_t *in, size_t len, uint8_t out[64])
{
    (void)mbedtls_sha512(in, len, out, 0);
}

void hsv3_ripemd160(const uint8_t *in, size_t len, uint8_t out[20])
{
    (void)mbedtls_ripemd160(in, len, out);
}

void hsv3_hash160(const uint8_t *in, size_t len, uint8_t out[20])
{
    uint8_t sha[32];
    hsv3_sha256(in, len, sha);
    hsv3_ripemd160(sha, sizeof(sha), out);
    hsv3_wipe(sha, sizeof(sha));
}

void hsv3_hmac_sha512(const uint8_t *key, size_t key_len,
                      const uint8_t *msg, size_t msg_len, uint8_t out[64])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    /* info nunca e NULL: MBEDTLS_SHA512_C esta ligado na configuracao. */
    (void)mbedtls_md_hmac(info, key, key_len, msg, msg_len, out);
}

int hsv3_pbkdf2_hmac_sha512(const uint8_t *pw, size_t pw_len,
                            const uint8_t *salt, size_t salt_len,
                            uint32_t iterations, uint8_t *out, size_t out_len)
{
    int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA512, pw, pw_len,
                                           salt, salt_len, iterations,
                                           (uint32_t)out_len, out);
    return rc == 0 ? HSV3_OK : HSV3_ERR_BACKEND;
}

/* --- secp256k1 -------------------------------------------------------- */

/* O mbedtls_ecp_mul aceita um RNG para "blinding" contra analise temporal.
 * Aqui a base e sempre G e o escalar e secreto, por isso o blinding importa.
 * O contexto e fornecido pelo chamador atraves de hsv3_ec_set_rng(); se
 * ninguem o definir, usa-se um contador determinista -- suficiente para os
 * testes no PC, e o firmware liga sempre o RNG real no arranque. */

static int (*s_rng_cb)(void *, unsigned char *, size_t) = NULL;
static void *s_rng_ctx = NULL;

void hsv3_ec_set_rng(int (*cb)(void *, unsigned char *, size_t), void *ctx)
{
    s_rng_cb = cb;
    s_rng_ctx = ctx;
}

static int fallback_rng(void *ctx, unsigned char *buf, size_t len)
{
    /* Nao e aleatoriedade de verdade e nao e usada para gerar chaves: serve so
     * de mascara no blinding do mbedtls quando corre no PC durante os testes. */
    static uint32_t counter = 0x9E3779B9u;
    (void)ctx;
    for (size_t i = 0; i < len; i++) {
        counter = counter * 1664525u + 1013904223u;
        buf[i] = (unsigned char)(counter >> 24);
    }
    return 0;
}

static int rng_wrapper(void *ctx, unsigned char *buf, size_t len)
{
    if (s_rng_cb != NULL) {
        return s_rng_cb(s_rng_ctx, buf, len);
    }
    return fallback_rng(ctx, buf, len);
}

int hsv3_ec_pubkey(const uint8_t priv[32], uint8_t out[33])
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi d;
    size_t olen = 0;
    int rc = HSV3_ERR_BACKEND;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d);

    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1) != 0) goto done;
    if (mbedtls_mpi_read_binary(&d, priv, 32) != 0) goto done;
    /* rejeita 0 e >= n antes de multiplicar */
    if (mbedtls_mpi_cmp_int(&d, 0) == 0) { rc = HSV3_ERR_RANGE; goto done; }
    if (mbedtls_mpi_cmp_mpi(&d, &grp.N) >= 0) { rc = HSV3_ERR_RANGE; goto done; }
    if (mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, rng_wrapper, NULL) != 0) goto done;
    if (mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_COMPRESSED,
                                       &olen, out, 33) != 0) goto done;
    rc = (olen == 33) ? HSV3_OK : HSV3_ERR_BACKEND;

done:
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    return rc;
}

int hsv3_ec_scalar_valid(const uint8_t k[32])
{
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    int rc = HSV3_ERR_RANGE;

    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);

    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1) != 0) {
        rc = HSV3_ERR_BACKEND;
        goto done;
    }
    if (mbedtls_mpi_read_binary(&d, k, 32) != 0) {
        rc = HSV3_ERR_BACKEND;
        goto done;
    }
    if (mbedtls_mpi_cmp_int(&d, 0) != 0 && mbedtls_mpi_cmp_mpi(&d, &grp.N) < 0) {
        rc = HSV3_OK;
    }

done:
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return rc;
}

int hsv3_ec_scalar_add(const uint8_t a[32], const uint8_t b[32], uint8_t out[32])
{
    mbedtls_ecp_group grp;
    mbedtls_mpi x, y, n;
    int rc = HSV3_ERR_BACKEND;

    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&x);
    mbedtls_mpi_init(&y);
    mbedtls_mpi_init(&n);

    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1) != 0) goto done;
    if (mbedtls_mpi_copy(&n, &grp.N) != 0) goto done;
    if (mbedtls_mpi_read_binary(&x, a, 32) != 0) goto done;
    if (mbedtls_mpi_read_binary(&y, b, 32) != 0) goto done;
    if (mbedtls_mpi_add_mpi(&x, &x, &y) != 0) goto done;
    if (mbedtls_mpi_mod_mpi(&x, &x, &n) != 0) goto done;
    if (mbedtls_mpi_cmp_int(&x, 0) == 0) { rc = HSV3_ERR_RANGE; goto done; }
    if (mbedtls_mpi_write_binary(&x, out, 32) != 0) goto done;
    rc = HSV3_OK;

done:
    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&y);
    mbedtls_mpi_free(&x);
    mbedtls_ecp_group_free(&grp);
    return rc;
}

void hsv3_wipe(void *buf, size_t len)
{
    mbedtls_platform_zeroize(buf, len);
}

int hsv3_memcmp_ct(const void *a, const void *b, size_t len)
{
    return mbedtls_ct_memcmp(a, b, len);
}
