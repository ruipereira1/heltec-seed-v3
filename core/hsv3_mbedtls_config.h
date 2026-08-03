/* Configuracao minima do mbedTLS para o HELTEC-SEED-V3.
 *
 * So o estritamente necessario para gerar uma seed: hashes, HMAC, PBKDF2 e
 * secp256k1. Nada de TLS, nada de sockets, nada de sistema de ficheiros.
 * Menos codigo compilado = menos superficie de ataque.
 *
 * Usado apenas no build de teste no PC. No ESP32 vale o sdkconfig do ESP-IDF,
 * que traz o mesmo mbedTLS -- e por isso que host e firmware concordam.
 */
#ifndef HSV3_MBEDTLS_CONFIG_H
#define HSV3_MBEDTLS_CONFIG_H

/* hashes */
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_RIPEMD160_C

/* HMAC + PBKDF2 (BIP39 usa PBKDF2-HMAC-SHA512, 2048 iteracoes) */
#define MBEDTLS_MD_C
#define MBEDTLS_PKCS5_C

/* curva */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256K1_ENABLED

/* utilitarios */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_ERROR_C

#endif /* HSV3_MBEDTLS_CONFIG_H */
