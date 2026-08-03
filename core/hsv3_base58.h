/* Base58Check, para serializar xpub/zpub/ypub. */
#ifndef HSV3_BASE58_H
#define HSV3_BASE58_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Acrescenta os 4 bytes de checksum (primeiros 4 de SHA256d) e codifica.
 * out_size tem de dar para o resultado mais o terminador. */
int hsv3_base58check_encode(const uint8_t *payload, size_t len,
                            char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* HSV3_BASE58_H */
