/* bech32 / bech32m (BIP-173 e BIP-350), so para mostrar enderecos.
 *
 * Serve para conferir contra o Sparrow: se o 1o endereco bater certo, a
 * derivacao inteira esta correcta. */
#ifndef HSV3_BECH32_H
#define HSV3_BECH32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HSV3_ADDR_MAX 96

/* witver 0 usa bech32, witver >= 1 usa bech32m. hrp e "bc" para mainnet. */
int hsv3_segwit_address(const char *hrp, int witver,
                        const uint8_t *prog, size_t prog_len,
                        char *out, size_t out_size);

/* Atalho para P2WPKH: endereco bc1q... a partir da chave publica comprimida. */
int hsv3_address_p2wpkh(const uint8_t pub[33], char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* HSV3_BECH32_H */
