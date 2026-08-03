#include "hsv3_base58.h"

#include <string.h>

#include "hsv3_crypto.h"

static const char B58[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

/* Chega para uma chave estendida: 78 bytes de payload + 4 de checksum. */
#define MAX_INPUT 96
/* log(256)/log(58) = 1.365..., mais folga e terminador */
#define MAX_OUTPUT (MAX_INPUT * 138 / 100 + 2)

int hsv3_base58check_encode(const uint8_t *payload, size_t len,
                            char *out, size_t out_size)
{
    uint8_t buf[MAX_INPUT];
    uint8_t digest[32];
    char tmp[MAX_OUTPUT];
    size_t zeros = 0, tmp_len = 0, total;

    if (payload == NULL || out == NULL) return HSV3_ERR_ARG;
    if (len + 4 > MAX_INPUT) return HSV3_ERR_RANGE;

    memcpy(buf, payload, len);
    hsv3_sha256(payload, len, digest);
    hsv3_sha256(digest, 32, digest);
    memcpy(buf + len, digest, 4);
    total = len + 4;

    while (zeros < total && buf[zeros] == 0) zeros++;

    /* Divisao longa por 58 sobre o array de bytes, ate sobrar zero. */
    for (size_t start = zeros; start < total; ) {
        uint32_t rem = 0;
        for (size_t i = start; i < total; i++) {
            uint32_t acc = (rem << 8) | buf[i];
            buf[i] = (uint8_t)(acc / 58);
            rem = acc % 58;
        }
        if (tmp_len >= sizeof(tmp)) return HSV3_ERR_RANGE;
        tmp[tmp_len++] = B58[rem];
        while (start < total && buf[start] == 0) start++;
    }

    if (zeros + tmp_len + 1 > out_size) return HSV3_ERR_RANGE;

    /* Um '1' por cada byte zero a cabeca, depois os digitos ao contrario. */
    for (size_t i = 0; i < zeros; i++) out[i] = '1';
    for (size_t i = 0; i < tmp_len; i++) out[zeros + i] = tmp[tmp_len - 1 - i];
    out[zeros + tmp_len] = '\0';

    hsv3_wipe(buf, sizeof(buf));
    hsv3_wipe(digest, sizeof(digest));
    hsv3_wipe(tmp, sizeof(tmp));
    return HSV3_OK;
}
