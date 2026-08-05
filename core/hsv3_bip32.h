/* BIP-32, so o que gerar uma seed exige.
 *
 * Nao ha derivacao publica (CKDpub): tudo o que mostramos sai de derivacao
 * privada seguida de multiplicacao escalar. Menos codigo, menos casos, menos
 * sitios onde se possa errar.
 */
#ifndef HSV3_BIP32_H
#define HSV3_BIP32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HSV3_HARDENED   0x80000000u
#define HSV3_XKEY_MAX   120   /* uma chave estendida em base58 tem 111 chars */

/* Versoes SLIP-0132. O prefixo so muda a apresentacao; a chave e a mesma.
 *
 * So as publicas mais o XPRV (usado nos testes contra os vetores oficiais do
 * BIP32). Faltam de proposito YPRV e ZPRV: este dispositivo nunca mostra uma
 * chave privada estendida no ecra, so as publicas de cada conta -- a mesma
 * razao por que nao ha CKDpub aqui em cima. */
#define HSV3_VER_XPRV 0x0488ADE4u
#define HSV3_VER_XPUB 0x0488B21Eu
#define HSV3_VER_YPUB 0x049D7CB2u   /* BIP49 */
#define HSV3_VER_ZPUB 0x04B24746u   /* BIP84 */

typedef struct {
    uint8_t  priv[32];
    uint8_t  pub[33];       /* comprimida, derivada de priv */
    uint8_t  chain[32];
    uint8_t  parent_fp[4];
    uint32_t index;
    uint8_t  depth;
} hsv3_hdkey_t;

/* HMAC-SHA512("Bitcoin seed", seed) -> chave mestra. */
int hsv3_bip32_master(const uint8_t *seed, size_t seed_len, hsv3_hdkey_t *out);

/* Um passo de derivacao privada. Devolve HSV3_ERR_RANGE no caso raro em que o
 * BIP32 manda saltar para o indice seguinte (probabilidade ~2^-127). */
int hsv3_bip32_ckd(const hsv3_hdkey_t *parent, uint32_t index, hsv3_hdkey_t *out);

/* Caminho no formato "m/84'/0'/0'/0/0". Aceita ' ou h para endurecido. */
int hsv3_bip32_derive(const hsv3_hdkey_t *root, const char *path,
                      hsv3_hdkey_t *out);

/* hash160(pub)[0..3] */
void hsv3_bip32_fingerprint(const hsv3_hdkey_t *k, uint8_t out[4]);

/* Serializacao base58check. is_private escolhe entre a metade prv e a pub. */
int hsv3_bip32_serialize(const hsv3_hdkey_t *k, uint32_t version, int is_private,
                         char *out, size_t out_size);

void hsv3_hdkey_wipe(hsv3_hdkey_t *k);

#ifdef __cplusplus
}
#endif

#endif /* HSV3_BIP32_H */
