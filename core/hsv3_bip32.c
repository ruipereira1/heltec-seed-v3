#include "hsv3_bip32.h"

#include <string.h>

#include "hsv3_base58.h"
#include "hsv3_crypto.h"

int hsv3_bip32_master(const uint8_t *seed, size_t seed_len, hsv3_hdkey_t *out)
{
    uint8_t I[64];
    int rc;

    if (seed == NULL || out == NULL) return HSV3_ERR_ARG;
    if (seed_len < 16 || seed_len > 64) return HSV3_ERR_ARG;

    memset(out, 0, sizeof(*out));
    hsv3_hmac_sha512((const uint8_t *)"Bitcoin seed", 12, seed, seed_len, I);

    rc = hsv3_ec_scalar_valid(I);
    if (rc != HSV3_OK) goto done;          /* IL == 0 ou >= n */

    memcpy(out->priv, I, 32);
    memcpy(out->chain, I + 32, 32);
    rc = hsv3_ec_pubkey(out->priv, out->pub);

done:
    hsv3_wipe(I, sizeof(I));
    if (rc != HSV3_OK) hsv3_wipe(out, sizeof(*out));
    return rc;
}

void hsv3_bip32_fingerprint(const hsv3_hdkey_t *k, uint8_t out[4])
{
    uint8_t h160[20];
    hsv3_hash160(k->pub, 33, h160);
    memcpy(out, h160, 4);
    hsv3_wipe(h160, sizeof(h160));
}

int hsv3_bip32_ckd(const hsv3_hdkey_t *parent, uint32_t index, hsv3_hdkey_t *out)
{
    uint8_t data[37];
    uint8_t I[64];
    hsv3_hdkey_t child;
    int rc;

    if (parent == NULL || out == NULL) return HSV3_ERR_ARG;

    if (index & HSV3_HARDENED) {
        data[0] = 0x00;
        memcpy(data + 1, parent->priv, 32);
    } else {
        memcpy(data, parent->pub, 33);
    }
    data[33] = (uint8_t)(index >> 24);
    data[34] = (uint8_t)(index >> 16);
    data[35] = (uint8_t)(index >> 8);
    data[36] = (uint8_t)(index);

    hsv3_hmac_sha512(parent->chain, 32, data, sizeof(data), I);

    memset(&child, 0, sizeof(child));

    /* ki = (IL + kpar) mod n. hsv3_ec_scalar_add ja recusa IL >= n atraves da
     * reducao, e recusa o resultado zero -- os dois casos em que o BIP32 manda
     * avancar para o indice seguinte. */
    rc = hsv3_ec_scalar_valid(I);
    if (rc != HSV3_OK) goto done;

    rc = hsv3_ec_scalar_add(I, parent->priv, child.priv);
    if (rc != HSV3_OK) goto done;

    memcpy(child.chain, I + 32, 32);
    rc = hsv3_ec_pubkey(child.priv, child.pub);
    if (rc != HSV3_OK) goto done;

    hsv3_bip32_fingerprint(parent, child.parent_fp);
    child.depth = (uint8_t)(parent->depth + 1);
    child.index = index;
    memcpy(out, &child, sizeof(child));

done:
    hsv3_wipe(data, sizeof(data));
    hsv3_wipe(I, sizeof(I));
    hsv3_wipe(&child, sizeof(child));
    if (rc != HSV3_OK) hsv3_wipe(out, sizeof(*out));
    return rc;
}

int hsv3_bip32_derive(const hsv3_hdkey_t *root, const char *path,
                      hsv3_hdkey_t *out)
{
    hsv3_hdkey_t cur, next;
    const char *p = path;
    int rc = HSV3_OK;

    if (root == NULL || path == NULL || out == NULL) return HSV3_ERR_ARG;

    memcpy(&cur, root, sizeof(cur));

    if (*p == 'm' || *p == 'M') p++;
    while (*p) {
        uint64_t n = 0;
        int digits = 0;
        uint32_t index;

        if (*p != '/') { rc = HSV3_ERR_ARG; goto done; }
        p++;
        if (!*p) { rc = HSV3_ERR_ARG; goto done; }   /* barra a solta no fim */

        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (uint64_t)(*p - '0');
            if (n > 0x7fffffffULL) { rc = HSV3_ERR_ARG; goto done; }
            p++;
            digits++;
        }
        if (digits == 0) { rc = HSV3_ERR_ARG; goto done; }

        index = (uint32_t)n;
        if (*p == '\'' || *p == 'h' || *p == 'H') {
            index |= HSV3_HARDENED;
            p++;
        }
        if (*p && *p != '/') { rc = HSV3_ERR_ARG; goto done; }

        rc = hsv3_bip32_ckd(&cur, index, &next);
        if (rc != HSV3_OK) goto done;
        hsv3_wipe(&cur, sizeof(cur));
        memcpy(&cur, &next, sizeof(cur));
        hsv3_wipe(&next, sizeof(next));
    }

    memcpy(out, &cur, sizeof(cur));

done:
    hsv3_wipe(&cur, sizeof(cur));
    hsv3_wipe(&next, sizeof(next));
    if (rc != HSV3_OK) hsv3_wipe(out, sizeof(*out));
    return rc;
}

int hsv3_bip32_serialize(const hsv3_hdkey_t *k, uint32_t version, int is_private,
                         char *out, size_t out_size)
{
    uint8_t payload[78];
    int rc;

    if (k == NULL || out == NULL) return HSV3_ERR_ARG;

    payload[0] = (uint8_t)(version >> 24);
    payload[1] = (uint8_t)(version >> 16);
    payload[2] = (uint8_t)(version >> 8);
    payload[3] = (uint8_t)(version);
    payload[4] = k->depth;
    memcpy(payload + 5, k->parent_fp, 4);
    payload[9]  = (uint8_t)(k->index >> 24);
    payload[10] = (uint8_t)(k->index >> 16);
    payload[11] = (uint8_t)(k->index >> 8);
    payload[12] = (uint8_t)(k->index);
    memcpy(payload + 13, k->chain, 32);
    if (is_private) {
        payload[45] = 0x00;
        memcpy(payload + 46, k->priv, 32);
    } else {
        memcpy(payload + 45, k->pub, 33);
    }

    rc = hsv3_base58check_encode(payload, sizeof(payload), out, out_size);
    hsv3_wipe(payload, sizeof(payload));
    return rc;
}

void hsv3_hdkey_wipe(hsv3_hdkey_t *k)
{
    hsv3_wipe(k, sizeof(*k));
}
