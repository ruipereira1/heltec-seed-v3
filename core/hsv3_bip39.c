#include "hsv3_bip39.h"

#include <string.h>

#include "hsv3_crypto.h"
#include "hsv3_wordlist.h"

/* Comprimentos de entropia validos: 128..256 bits em passos de 32. */
static int ent_len_valid(size_t n)
{
    return n == 16 || n == 20 || n == 24 || n == 28 || n == 32;
}

size_t hsv3_bip39_word_count(size_t ent_len)
{
    if (!ent_len_valid(ent_len)) return 0;
    return (ent_len * 8 + ent_len * 8 / 32) / 11;
}

int hsv3_word_index(const char *word)
{
    /* A wordlist esta ordenada, por isso da para pesquisa binaria. */
    int lo = 0, hi = HSV3_WORDLIST_SIZE - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = strcmp(word, hsv3_wordlist[mid]);
        if (cmp == 0) return mid;
        if (cmp < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return -1;
}

/* Bit i da concatenacao entropia||checksum. */
static uint8_t bit_at(const uint8_t *ent, size_t ent_len, uint8_t cs, size_t i)
{
    size_t ent_bits = ent_len * 8;
    if (i < ent_bits) {
        return (uint8_t)((ent[i / 8] >> (7 - (i % 8))) & 1u);
    }
    return (uint8_t)((cs >> (7 - (i - ent_bits))) & 1u);
}

int hsv3_bip39_word_at(const uint8_t *ent, size_t ent_len, size_t i,
                       uint16_t *out_index)
{
    uint8_t digest[32];
    uint16_t idx = 0;

    if (!ent_len_valid(ent_len) || out_index == NULL) return HSV3_ERR_ARG;
    if (i >= hsv3_bip39_word_count(ent_len)) return HSV3_ERR_ARG;

    /* O checksum sao os primeiros ent_len*8/32 bits do SHA256 da entropia.
     * Para ent_len <= 32 isso nunca passa de 8 bits, logo cabe no 1o byte. */
    hsv3_sha256(ent, ent_len, digest);

    for (size_t b = 0; b < 11; b++) {
        idx = (uint16_t)((idx << 1) | bit_at(ent, ent_len, digest[0], i * 11 + b));
    }
    hsv3_wipe(digest, sizeof(digest));

    *out_index = idx;
    return HSV3_OK;
}

int hsv3_bip39_from_entropy(const uint8_t *ent, size_t ent_len,
                            char *out, size_t out_size)
{
    size_t words = hsv3_bip39_word_count(ent_len);
    size_t pos = 0;

    if (words == 0 || out == NULL) return HSV3_ERR_ARG;

    for (size_t i = 0; i < words; i++) {
        uint16_t idx;
        int rc = hsv3_bip39_word_at(ent, ent_len, i, &idx);
        if (rc != HSV3_OK) return rc;

        const char *w = hsv3_wordlist[idx];
        size_t wl = strlen(w);
        if (pos + wl + (i ? 1 : 0) + 1 > out_size) return HSV3_ERR_RANGE;
        if (i) out[pos++] = ' ';
        memcpy(out + pos, w, wl);
        pos += wl;
    }
    out[pos] = '\0';
    return HSV3_OK;
}

int hsv3_bip39_to_entropy(const char *mnemonic, uint8_t *out, size_t out_size,
                          size_t *out_len)
{
    uint16_t idx[HSV3_BIP39_MAX_WORDS];
    char word[HSV3_WORD_MAXLEN + 2];
    size_t nwords = 0, ent_len, cs_bits;
    uint8_t digest[32];
    const char *p = mnemonic;
    int rc = HSV3_ERR_ARG;

    if (mnemonic == NULL || out == NULL) return HSV3_ERR_ARG;

    while (*p) {
        size_t wl = 0;
        while (*p == ' ') p++;
        if (!*p) break;
        while (*p && *p != ' ') {
            if (wl >= HSV3_WORD_MAXLEN + 1) return HSV3_ERR_ARG;  /* palavra longa demais */
            word[wl++] = *p++;
        }
        word[wl] = '\0';
        if (nwords >= HSV3_BIP39_MAX_WORDS) return HSV3_ERR_ARG;
        int w = hsv3_word_index(word);
        if (w < 0) return HSV3_ERR_ARG;
        idx[nwords++] = (uint16_t)w;
    }

    if (nwords < 12 || nwords > 24 || nwords % 3 != 0) return HSV3_ERR_ARG;

    ent_len = nwords * 11 * 32 / 33 / 8;
    cs_bits = ent_len * 8 / 32;
    if (!ent_len_valid(ent_len) || out_size < ent_len) return HSV3_ERR_RANGE;

    /* Desempacota os 11 bits de cada palavra de volta para bytes. */
    memset(out, 0, ent_len);
    uint8_t cs_got = 0;
    for (size_t i = 0; i < nwords; i++) {
        for (size_t b = 0; b < 11; b++) {
            size_t bit = i * 11 + b;
            uint8_t v = (uint8_t)((idx[i] >> (10 - b)) & 1u);
            if (bit < ent_len * 8) {
                out[bit / 8] |= (uint8_t)(v << (7 - (bit % 8)));
            } else {
                cs_got = (uint8_t)((cs_got << 1) | v);
            }
        }
    }

    hsv3_sha256(out, ent_len, digest);
    uint8_t cs_want = (uint8_t)(digest[0] >> (8 - cs_bits));
    hsv3_wipe(digest, sizeof(digest));

    if (cs_got != cs_want) {
        hsv3_wipe(out, ent_len);
        return HSV3_ERR_RANGE;   /* checksum BIP39 invalido */
    }

    if (out_len) *out_len = ent_len;
    rc = HSV3_OK;
    return rc;
}

int hsv3_bip39_passphrase_valid(const char *passphrase)
{
    if (passphrase == NULL) return HSV3_ERR_ARG;
    for (const unsigned char *p = (const unsigned char *)passphrase; *p; p++) {
        if (*p < 32 || *p > 126) return HSV3_ERR_ARG;
    }
    return HSV3_OK;
}

int hsv3_bip39_seed(const char *mnemonic, const char *passphrase,
                    uint8_t out[HSV3_SEED_LEN])
{
    /* "mnemonic" + passphrase; a passphrase e limitada a ASCII imprimivel, por
     * isso o buffer chega bem. */
    char salt[8 + 256 + 1];
    size_t plen;
    int rc;

    if (mnemonic == NULL || out == NULL) return HSV3_ERR_ARG;
    if (passphrase == NULL) passphrase = "";
    if (hsv3_bip39_passphrase_valid(passphrase) != HSV3_OK) return HSV3_ERR_ARG;

    plen = strlen(passphrase);
    if (plen > 256) return HSV3_ERR_RANGE;

    memcpy(salt, "mnemonic", 8);
    memcpy(salt + 8, passphrase, plen);
    salt[8 + plen] = '\0';

    rc = hsv3_pbkdf2_hmac_sha512((const uint8_t *)mnemonic, strlen(mnemonic),
                                 (const uint8_t *)salt, 8 + plen,
                                 2048, out, HSV3_SEED_LEN);
    hsv3_wipe(salt, sizeof(salt));
    return rc;
}
