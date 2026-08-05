/* Primitivas criptograficas de que o HELTEC-SEED-V3 precisa -- e mais nenhuma.
 *
 * O nucleo (entropia, BIP39, BIP32) so fala com este cabecalho. Assim a
 * biblioteca por baixo pode ser trocada sem tocar na logica, e a logica pode
 * ser testada no PC exatamente com o mesmo backend que corre no ESP32.
 *
 * Como so geramos seeds -- nunca assinamos -- a superficie e pequena: nao ha
 * ECDSA, nao ha derivacao publica (CKDpub), nao ha parsing de PSBT.
 */
#ifndef HSV3_CRYPTO_H
#define HSV3_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HSV3_OK              0
#define HSV3_ERR_BACKEND    -1   /* falha interna da biblioteca cripto */
#define HSV3_ERR_RANGE      -2   /* escalar fora de [1, n-1] */
#define HSV3_ERR_ARG        -3   /* argumento invalido */

void hsv3_sha256(const uint8_t *in, size_t len, uint8_t out[32]);
void hsv3_sha512(const uint8_t *in, size_t len, uint8_t out[64]);
void hsv3_ripemd160(const uint8_t *in, size_t len, uint8_t out[20]);

/* hash160 = RIPEMD160(SHA256(x)), usado no fingerprint e nos enderecos */
void hsv3_hash160(const uint8_t *in, size_t len, uint8_t out[20]);

void hsv3_hmac_sha512(const uint8_t *key, size_t key_len,
                      const uint8_t *msg, size_t msg_len, uint8_t out[64]);

/* BIP39: PBKDF2-HMAC-SHA512, 2048 iteracoes, 64 bytes de saida */
int hsv3_pbkdf2_hmac_sha512(const uint8_t *pw, size_t pw_len,
                            const uint8_t *salt, size_t salt_len,
                            uint32_t iterations, uint8_t *out, size_t out_len);

/* secp256k1 -- so o que a derivacao privada BIP32 exige */

/* Chave publica comprimida (SEC1, 33 bytes) a partir da privada. */
int hsv3_ec_pubkey(const uint8_t priv[32], uint8_t out[33]);

/* Verifica 0 < k < n. Devolve HSV3_OK ou HSV3_ERR_RANGE. */
int hsv3_ec_scalar_valid(const uint8_t k[32]);

/* out = (a + b) mod n. Devolve HSV3_ERR_RANGE se o resultado for zero,
 * caso em que o BIP32 manda avancar para o indice seguinte. */
int hsv3_ec_scalar_add(const uint8_t a[32], const uint8_t b[32], uint8_t out[32]);

/* Instala o RNG usado para mascarar a multiplicacao escalar (blinding contra
 * analise temporal). O firmware chama isto no arranque com o RNG real; se
 * ninguem chamar, o backend usa uma mascara determinista -- que nunca gera
 * chaves, so mascara -- suficiente para os testes no PC. */
void hsv3_ec_set_rng(int (*cb)(void *, unsigned char *, size_t), void *ctx);

/* Apaga memoria sensivel sem o compilador poder otimizar a chamada. */
void hsv3_wipe(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HSV3_CRYPTO_H */
