#include "hsv3_bech32.h"

#include <string.h>

#include "hsv3_crypto.h"

static const char CHARSET[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

#define BECH32M_CONST 0x2bc830a3u

static uint32_t polymod(const uint8_t *values, size_t len)
{
    static const uint32_t GEN[5] = {
        0x3b6a57b2u, 0x26508e6du, 0x1ea119fau, 0x3d4233ddu, 0x2a1462b3u
    };
    uint32_t chk = 1;
    for (size_t i = 0; i < len; i++) {
        uint32_t top = chk >> 25;
        chk = ((chk & 0x1ffffffu) << 5) ^ values[i];
        for (int j = 0; j < 5; j++) {
            if ((top >> j) & 1u) chk ^= GEN[j];
        }
    }
    return chk;
}

int hsv3_segwit_address(const char *hrp, int witver,
                        const uint8_t *prog, size_t prog_len,
                        char *out, size_t out_size)
{
    uint8_t values[HSV3_ADDR_MAX];
    size_t hrp_len, n = 0, pos = 0;
    uint32_t acc = 0, chk;
    int bits = 0;

    if (hrp == NULL || prog == NULL || out == NULL) return HSV3_ERR_ARG;
    if (witver < 0 || witver > 16) return HSV3_ERR_ARG;
    if (prog_len < 2 || prog_len > 40) return HSV3_ERR_ARG;
    if (witver == 0 && prog_len != 20 && prog_len != 32) return HSV3_ERR_ARG;

    hrp_len = strlen(hrp);
    if (hrp_len == 0 || hrp_len > 8) return HSV3_ERR_ARG;

    /* expansao do hrp: bits altos, separador, bits baixos */
    for (size_t i = 0; i < hrp_len; i++) values[n++] = (uint8_t)(hrp[i] >> 5);
    values[n++] = 0;
    for (size_t i = 0; i < hrp_len; i++) values[n++] = (uint8_t)(hrp[i] & 31);

    size_t data_start = n;
    values[n++] = (uint8_t)witver;

    /* 8 bits -> 5 bits */
    for (size_t i = 0; i < prog_len; i++) {
        acc = (acc << 8) | prog[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            if (n >= sizeof(values)) return HSV3_ERR_RANGE;
            values[n++] = (uint8_t)((acc >> bits) & 31u);
        }
    }
    if (bits > 0) {
        if (n >= sizeof(values)) return HSV3_ERR_RANGE;
        values[n++] = (uint8_t)((acc << (5 - bits)) & 31u);
    }
    size_t data_len = n - data_start;

    /* checksum */
    for (int i = 0; i < 6; i++) {
        if (n >= sizeof(values)) return HSV3_ERR_RANGE;
        values[n++] = 0;
    }
    chk = polymod(values, n) ^ (witver == 0 ? 1u : BECH32M_CONST);
    for (int i = 0; i < 6; i++) {
        values[data_start + data_len + (size_t)i] =
            (uint8_t)((chk >> (5 * (5 - i))) & 31u);
    }

    if (hrp_len + 1 + data_len + 6 + 1 > out_size) return HSV3_ERR_RANGE;
    memcpy(out, hrp, hrp_len);
    pos = hrp_len;
    out[pos++] = '1';
    for (size_t i = 0; i < data_len + 6; i++) {
        out[pos++] = CHARSET[values[data_start + i]];
    }
    out[pos] = '\0';
    return HSV3_OK;
}

int hsv3_address_p2wpkh(const uint8_t pub[33], char *out, size_t out_size)
{
    uint8_t h160[20];
    int rc;

    if (pub == NULL) return HSV3_ERR_ARG;
    hsv3_hash160(pub, 33, h160);
    rc = hsv3_segwit_address("bc", 0, h160, sizeof(h160), out, out_size);
    hsv3_wipe(h160, sizeof(h160));
    return rc;
}
