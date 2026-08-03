/* BIP-39: entropia <-> mnemonica, e mnemonica -> seed. */
#ifndef HSV3_BIP39_H
#define HSV3_BIP39_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HSV3_BIP39_MAX_WORDS 24
/* 24 palavras de 8 caracteres + 23 espacos + terminador */
#define HSV3_MNEMONIC_MAX    (24 * 8 + 23 + 1)
#define HSV3_SEED_LEN        64

/* Indice da palavra na wordlist, ou -1. Comparacao exacta, sem prefixos. */
int hsv3_word_index(const char *word);

/* Numero de palavras para uma entropia de ent_len bytes (0 se invalido). */
size_t hsv3_bip39_word_count(size_t ent_len);

/* Indice (0..2047) da palavra i. Permite mostrar o ecra sem materializar a
 * mnemonica inteira em RAM. */
int hsv3_bip39_word_at(const uint8_t *ent, size_t ent_len, size_t i,
                       uint16_t *out_index);

/* Mnemonica completa, palavras separadas por um espaco. */
int hsv3_bip39_from_entropy(const uint8_t *ent, size_t ent_len,
                            char *out, size_t out_size);

/* Caminho inverso, com validacao do checksum BIP39. */
int hsv3_bip39_to_entropy(const char *mnemonic, uint8_t *out, size_t out_size,
                          size_t *out_len);

/* PBKDF2-HMAC-SHA512(mnemonica, "mnemonic"+passphrase, 2048) -> 64 bytes.
 * A passphrase e usada tal e qual: como so aceitamos ASCII 32..126, a
 * normalizacao NFKD que o BIP39 exige e a identidade, e nao ha ambiguidade
 * possivel contra o Sparrow ou a Coldcard. */
int hsv3_bip39_seed(const char *mnemonic, const char *passphrase,
                    uint8_t out[HSV3_SEED_LEN]);

/* HSV3_OK se a passphrase for ASCII imprimivel (32..126) ou vazia. */
int hsv3_bip39_passphrase_valid(const char *passphrase);

#ifdef __cplusplus
}
#endif

#endif /* HSV3_BIP39_H */
