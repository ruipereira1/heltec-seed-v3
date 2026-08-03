/* Teste rapido: o mbedTLS compila e as primitivas dao os valores certos? */
#include <stdio.h>
#include <string.h>
#include "hsv3_crypto.h"

static int fails = 0;

static void hex(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]);
}

static void check(const char *name, const uint8_t *got, size_t n, const char *want)
{
    char s[160];
    hex(got, n, s);
    if (strcmp(s, want) != 0) {
        printf("  [X] %s\n      obtido:   %s\n      esperado: %s\n", name, s, want);
        fails++;
    }
}

int main(void)
{
    uint8_t out[64];

    hsv3_sha256((const uint8_t *)"abc", 3, out);
    check("sha256('abc')", out, 32,
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    hsv3_sha512((const uint8_t *)"abc", 3, out);
    check("sha512('abc')", out, 64,
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
          "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    hsv3_ripemd160((const uint8_t *)"abc", 3, out);
    check("ripemd160('abc')", out, 20, "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc");

    /* RFC 4231 caso 1 */
    {
        uint8_t key[20];
        memset(key, 0x0b, sizeof(key));
        hsv3_hmac_sha512(key, sizeof(key), (const uint8_t *)"Hi There", 8, out);
        check("hmac-sha512 rfc4231#1", out, 64,
              "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
              "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854");
    }

    /* BIP32: chave mestra do vetor 1 -> pubkey comprimida */
    {
        const uint8_t seed[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        uint8_t I[64], pub[33];
        hsv3_hmac_sha512((const uint8_t *)"Bitcoin seed", 12, seed, sizeof(seed), I);
        check("BIP32 v1 IL (priv mestra)", I, 32,
              "e8f32e723decf4051aefac8e2c93c9c5b214313817cdb01a1494b917c8436b35");
        if (hsv3_ec_pubkey(I, pub) != HSV3_OK) {
            printf("  [X] hsv3_ec_pubkey falhou\n");
            fails++;
        } else {
            check("BIP32 v1 pubkey mestra", pub, 33,
                  "0339a36013301597daef41fbe593a02cc513d0b55527ec2df1050e2e8ff49c85c2");
        }
    }

    /* BIP39: PBKDF2-HMAC-SHA512 com 2048 iteracoes */
    {
        const char *mn = "abandon abandon abandon abandon abandon abandon abandon "
                         "abandon abandon abandon abandon about";
        const char *salt = "mnemonicTREZOR";
        if (hsv3_pbkdf2_hmac_sha512((const uint8_t *)mn, strlen(mn),
                                    (const uint8_t *)salt, strlen(salt),
                                    2048, out, 64) != HSV3_OK) {
            printf("  [X] pbkdf2 falhou\n");
            fails++;
        } else {
            check("BIP39 seed (12x abandon)", out, 64,
                  "c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e5349553"
                  "1f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04");
        }
    }

    /* escalares */
    {
        uint8_t n_minus_1[32] = {
            0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe,
            0xba,0xae,0xdc,0xe6,0xaf,0x48,0xa0,0x3b,0xbf,0xd2,0x5e,0x8c,0xd0,0x36,0x41,0x40};
        uint8_t one[32] = {0}, zero[32] = {0}, sum[32];
        one[31] = 1;
        if (hsv3_ec_scalar_valid(zero) != HSV3_ERR_RANGE) { printf("  [X] zero aceite\n"); fails++; }
        if (hsv3_ec_scalar_valid(one) != HSV3_OK) { printf("  [X] 1 rejeitado\n"); fails++; }
        if (hsv3_ec_scalar_valid(n_minus_1) != HSV3_OK) { printf("  [X] n-1 rejeitado\n"); fails++; }
        /* (n-1) + 1 = n = 0 mod n -> tem de ser recusado */
        if (hsv3_ec_scalar_add(n_minus_1, one, sum) != HSV3_ERR_RANGE) {
            printf("  [X] (n-1)+1 devia dar zero mod n e ser recusado\n"); fails++;
        }
    }

    if (fails) { printf("\nsmoke: %d FALHAS\n", fails); return 1; }
    printf("smoke: todas as primitivas OK (sha256/512, ripemd160, hmac, pbkdf2, secp256k1)\n");
    return 0;
}
