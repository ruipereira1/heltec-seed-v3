/* Testes do nucleo contra os vetores oficiais.
 *
 * Corre no PC com o mesmo codigo e o mesmo backend cripto (mbedTLS) que o
 * firmware usa no ESP32-S3. Se algo falhar aqui, falha na placa.
 *
 *   sh test/build.sh
 */
#include <stdio.h>
#include <string.h>

#include "hsv3_base58.h"
#include "hsv3_bech32.h"
#include "hsv3_bip32.h"
#include "hsv3_bip39.h"
#include "hsv3_crypto.h"
#include "hsv3_entropy.h"
#include "hsv3_wordlist.h"
#include "vectors_generated.h"

static int checks = 0;
static int fails = 0;

static void fail(const char *name, const char *got, const char *want)
{
    printf("  [X] %s\n      obtido:   %s\n      esperado: %s\n", name, got, want);
    fails++;
}

static void eq_str(const char *name, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) fail(name, got, want);
}

static void eq_hex(const char *name, const uint8_t *got, size_t n, const char *want)
{
    char s[200];
    checks++;
    for (size_t i = 0; i < n; i++) sprintf(s + i * 2, "%02x", got[i]);
    s[n * 2] = '\0';
    if (strcmp(s, want) != 0) fail(name, s, want);
}

static void ok(const char *name, int cond)
{
    checks++;
    if (!cond) { printf("  [X] %s\n", name); fails++; }
}

static size_t unhex(const char *hex, uint8_t *out, size_t out_size)
{
    size_t n = strlen(hex) / 2;
    if (n > out_size) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + i * 2, "%2x", &v);
        out[i] = (uint8_t)v;
    }
    return n;
}

/* --------------------------------------------------------------------- */

static void test_wordlist(void)
{
    eq_str("wordlist[0]", hsv3_wordlist[0], "abandon");
    eq_str("wordlist[2047]", hsv3_wordlist[2047], "zoo");
    ok("word_index('abandon') == 0", hsv3_word_index("abandon") == 0);
    ok("word_index('zoo') == 2047", hsv3_word_index("zoo") == 2047);
    ok("word_index('about') == 3", hsv3_word_index("about") == 3);
    ok("word_index('nao-existe') < 0", hsv3_word_index("nonexistent") < 0);
    /* prefixo de uma palavra real nao pode passar */
    ok("word_index('aban') < 0", hsv3_word_index("aban") < 0);
    ok("word_index('') < 0", hsv3_word_index("") < 0);

    /* a lista tem de estar ordenada, senao a pesquisa binaria mente */
    int sorted = 1;
    for (int i = 1; i < HSV3_WORDLIST_SIZE; i++) {
        if (strcmp(hsv3_wordlist[i - 1], hsv3_wordlist[i]) >= 0) { sorted = 0; break; }
    }
    ok("wordlist ordenada", sorted);
}

static void test_bip39(void)
{
    char name[80];
    for (int i = 0; i < BIP39_VEC_COUNT; i++) {
        const bip39_vec_t *v = &BIP39_VECTORS[i];
        uint8_t ent[32], seed[64], back[32];
        size_t ent_len, back_len = 0;
        char mn[HSV3_MNEMONIC_MAX];
        hsv3_hdkey_t master;
        char xprv[HSV3_XKEY_MAX];

        ent_len = unhex(v->entropy, ent, sizeof(ent));

        sprintf(name, "BIP39[%d] mnemonica", i);
        ok(name, hsv3_bip39_from_entropy(ent, ent_len, mn, sizeof(mn)) == HSV3_OK);
        eq_str(name, mn, v->mnemonic);

        sprintf(name, "BIP39[%d] roundtrip", i);
        ok(name, hsv3_bip39_to_entropy(mn, back, sizeof(back), &back_len) == HSV3_OK);
        checks++;
        if (back_len != ent_len || memcmp(back, ent, ent_len) != 0) {
            printf("  [X] %s\n", name);
            fails++;
        }

        sprintf(name, "BIP39[%d] seed", i);
        ok(name, hsv3_bip39_seed(mn, "TREZOR", seed) == HSV3_OK);
        eq_hex(name, seed, 64, v->seed);

        sprintf(name, "BIP39[%d] xprv raiz", i);
        ok(name, hsv3_bip32_master(seed, 64, &master) == HSV3_OK);
        ok(name, hsv3_bip32_serialize(&master, HSV3_VER_XPRV, 1,
                                      xprv, sizeof(xprv)) == HSV3_OK);
        eq_str(name, xprv, v->xprv);
        hsv3_hdkey_wipe(&master);
    }
}

static void test_bip32(void)
{
    char name[96];
    for (int i = 0; i < BIP32_VEC_COUNT; i++) {
        const bip32_vec_t *v = &BIP32_VECTORS[i];
        uint8_t seed[64];
        size_t seed_len = unhex(v->seed, seed, sizeof(seed));
        hsv3_hdkey_t root, node;
        char s[HSV3_XKEY_MAX];

        sprintf(name, "BIP32[%d] %s master", i, v->path);
        ok(name, hsv3_bip32_master(seed, seed_len, &root) == HSV3_OK);

        sprintf(name, "BIP32[%d] %s derive", i, v->path);
        ok(name, hsv3_bip32_derive(&root, v->path, &node) == HSV3_OK);

        sprintf(name, "BIP32[%d] %s xpub", i, v->path);
        ok(name, hsv3_bip32_serialize(&node, HSV3_VER_XPUB, 0, s, sizeof(s)) == HSV3_OK);
        eq_str(name, s, v->xpub);

        sprintf(name, "BIP32[%d] %s xprv", i, v->path);
        ok(name, hsv3_bip32_serialize(&node, HSV3_VER_XPRV, 1, s, sizeof(s)) == HSV3_OK);
        eq_str(name, s, v->xprv);

        hsv3_hdkey_wipe(&node);
        hsv3_hdkey_wipe(&root);
    }
}

static void test_bip84(void)
{
    uint8_t seed[64];
    hsv3_hdkey_t root, acct, node;
    char s[HSV3_XKEY_MAX];
    char addr[HSV3_ADDR_MAX];

    ok("BIP84 seed", hsv3_bip39_seed(BIP84_MNEMONIC, "", seed) == HSV3_OK);
    ok("BIP84 master", hsv3_bip32_master(seed, 64, &root) == HSV3_OK);
    ok("BIP84 conta", hsv3_bip32_derive(&root, "m/84'/0'/0'", &acct) == HSV3_OK);
    ok("BIP84 zpub ser", hsv3_bip32_serialize(&acct, HSV3_VER_ZPUB, 0, s, sizeof(s)) == HSV3_OK);
    eq_str("BIP84 zpub", s, BIP84_ZPUB);

    ok("BIP84 0/0", hsv3_bip32_derive(&acct, "m/0/0", &node) == HSV3_OK);
    ok("BIP84 addr0 enc", hsv3_address_p2wpkh(node.pub, addr, sizeof(addr)) == HSV3_OK);
    eq_str("BIP84 endereco 0/0", addr, BIP84_ADDR0);

    ok("BIP84 0/1", hsv3_bip32_derive(&acct, "m/0/1", &node) == HSV3_OK);
    ok("BIP84 addr1 enc", hsv3_address_p2wpkh(node.pub, addr, sizeof(addr)) == HSV3_OK);
    eq_str("BIP84 endereco 0/1", addr, BIP84_ADDR1);

    ok("BIP84 1/0", hsv3_bip32_derive(&acct, "m/1/0", &node) == HSV3_OK);
    ok("BIP84 change enc", hsv3_address_p2wpkh(node.pub, addr, sizeof(addr)) == HSV3_OK);
    eq_str("BIP84 endereco troco 1/0", addr, BIP84_CHANGE0);

    hsv3_hdkey_wipe(&node);
    hsv3_hdkey_wipe(&acct);
    hsv3_hdkey_wipe(&root);
}

/* A parte que mais interessa: a entropia tem de recusar, nunca degradar. */
static void test_entropy(void)
{
    hsv3_phys_t p;
    hsv3_chip_t c;
    uint8_t d[32], chip[32], mixed[32], zeros[32], ones[32];
    char hex[65];

    memset(zeros, 0x00, sizeof(zeros));
    memset(ones, 0xff, sizeof(ones));

    /* --- dados --- */
    hsv3_phys_init(&p, HSV3_PHYS_DICE);
    ok("dados: recusa digito 0", hsv3_phys_push(&p, '0') == HSV3_ERR_ARG);
    ok("dados: recusa digito 7", hsv3_phys_push(&p, '7') == HSV3_ERR_ARG);
    ok("dados: recusa letra", hsv3_phys_push(&p, 'a') == HSV3_ERR_ARG);
    ok("dados: aceita 1..6", hsv3_phys_push(&p, '1') == HSV3_OK
                          && hsv3_phys_push(&p, '6') == HSV3_OK);
    ok("dados: pop desfaz", hsv3_phys_pop(&p) == HSV3_OK && p.count == 1);
    ok("dados: pop no vazio falha", hsv3_phys_pop(&p) == HSV3_OK
                                 && hsv3_phys_pop(&p) == HSV3_ERR_RANGE);

    /* 99 lancamentos tem de ser recusado; 100 tem de passar */
    hsv3_phys_init(&p, HSV3_PHYS_DICE);
    for (int i = 0; i < 99; i++) hsv3_phys_push(&p, '1');
    ok("dados: 99 lancamentos e' pouco", hsv3_phys_enough(&p) == 0);
    ok("dados: 99 -> digest recusado", hsv3_phys_digest(&p, d) == HSV3_ERR_RANGE);
    hsv3_phys_push(&p, '1');
    ok("dados: 100 chega", hsv3_phys_enough(&p) == 1);
    ok("dados: 100 -> digest ok", hsv3_phys_digest(&p, d) == HSV3_OK);

    /* tem de ser exatamente SHA256("111...1"), para o verify.py reproduzir */
    {
        uint8_t want[32];
        char buf[101];
        memset(buf, '1', 100);
        hsv3_sha256((const uint8_t *)buf, 100, want);
        checks++;
        if (memcmp(d, want, 32) != 0) {
            printf("  [X] dados: digest nao e' SHA256 dos digitos ASCII\n");
            fails++;
        }
    }

    /* --- moeda --- */
    hsv3_phys_init(&p, HSV3_PHYS_COIN);
    ok("moeda: recusa digito 2", hsv3_phys_push(&p, '2') == HSV3_ERR_ARG);
    for (int i = 0; i < 255; i++) hsv3_phys_push(&p, (char)('0' + (i & 1)));
    ok("moeda: 255 e' pouco", hsv3_phys_digest(&p, d) == HSV3_ERR_RANGE);
    hsv3_phys_push(&p, '0');
    ok("moeda: 256 chega", hsv3_phys_digest(&p, d) == HSV3_OK);

    /* --- chip: fail-closed --- */
    hsv3_chip_init(&c);
    ok("chip: sem fontes -> recusa", hsv3_chip_digest(&c, chip) == HSV3_ERR_RANGE);
    hsv3_chip_set(&c, HSV3_SRC_TRNG, d);
    ok("chip: 1 de 3 -> recusa", hsv3_chip_digest(&c, chip) == HSV3_ERR_RANGE);
    hsv3_chip_set(&c, HSV3_SRC_JITTER, d);
    ok("chip: 2 de 3 -> recusa", hsv3_chip_digest(&c, chip) == HSV3_ERR_RANGE);
    hsv3_chip_set(&c, HSV3_SRC_TIMING, d);
    ok("chip: 3 de 3 -> ok", hsv3_chip_digest(&c, chip) == HSV3_OK);

    /* fonte morta tem de ser apanhada */
    hsv3_chip_set(&c, HSV3_SRC_TRNG, zeros);
    ok("chip: TRNG a zeros -> recusa", hsv3_chip_digest(&c, chip) == HSV3_ERR_RANGE);
    hsv3_chip_set(&c, HSV3_SRC_TRNG, ones);
    ok("chip: TRNG a 0xff -> recusa", hsv3_chip_digest(&c, chip) == HSV3_ERR_RANGE);

    /* Padroes curtos a repetir sao o aspecto de um registo preso ou de um
     * barramento a devolver sempre a mesma palavra. A versao anterior do
     * looks_dead so via "todos os bytes iguais" e deixava passar isto. */
    {
        uint8_t p[32];
        static const size_t periodos[] = { 2, 4, 8, 16 };
        char nome[64];
        for (size_t k = 0; k < sizeof(periodos) / sizeof(periodos[0]); k++) {
            for (size_t i = 0; i < 32; i++) p[i] = (uint8_t)(0xa0 + (i % periodos[k]));
            hsv3_chip_set(&c, HSV3_SRC_TRNG, p);
            sprintf(nome, "chip: padrao a repetir de %u bytes -> recusa",
                    (unsigned)periodos[k]);
            ok(nome, hsv3_chip_digest(&c, chip) == HSV3_ERR_RANGE);
        }
        /* E um padrao de 17 bytes (nao divide 32) tem de passar: o teste nao
         * pode ser tao largo que recuse entropia boa. */
        for (size_t i = 0; i < 32; i++) p[i] = (uint8_t)(0xa0 + (i % 17));
        hsv3_chip_set(&c, HSV3_SRC_TRNG, p);
        ok("chip: periodo que nao divide 32 -> aceite",
           hsv3_chip_digest(&c, chip) == HSV3_OK);
    }

    hsv3_chip_set(&c, HSV3_SRC_TRNG, d);
    ok("chip: recuperado -> ok", hsv3_chip_digest(&c, chip) == HSV3_OK);

    /* --- compromisso ---
     *
     * O que se testa aqui e' so que C = SHA256(E_chip) e que muda quando o
     * E_chip muda. A propriedade que interessa -- o compromisso ser mostrado
     * ANTES dos lancamentos -- e' da ordem da cerimonia em main/main.c, e nao
     * ha teste no PC que a cubra: ve-se lendo entropy_round(). */
    {
        uint8_t commit[32], want[32], other[32];

        ok("commit: recusa ponteiro nulo",
           hsv3_entropy_commit(NULL, commit) == HSV3_ERR_ARG);
        ok("commit: aceita E_chip valido",
           hsv3_entropy_commit(chip, commit) == HSV3_OK);

        hsv3_sha256(chip, 32, want);
        checks++;
        if (memcmp(commit, want, 32) != 0) {
            printf("  [X] commit: nao e' SHA256 dos 32 bytes do E_chip\n");
            fails++;
        }

        /* Um bit trocado no E_chip tem de dar outro compromisso, senao o
         * verify.py deixava passar um E_chip substituido. */
        memcpy(other, chip, 32);
        other[0] ^= 0x01;
        ok("commit: muda com o E_chip",
           hsv3_entropy_commit(other, want) == HSV3_OK && memcmp(commit, want, 32) != 0);
    }

    /* --- XOR --- */
    ok("xor: identidade", hsv3_entropy_combine(d, zeros, mixed) == HSV3_OK
                       && memcmp(mixed, d, 32) == 0);
    ok("xor: anula-se", hsv3_entropy_combine(d, d, mixed) == HSV3_OK
                     && memcmp(mixed, zeros, 32) == 0);
    hsv3_entropy_combine(d, ones, mixed);
    hsv3_entropy_combine(mixed, ones, mixed);
    ok("xor: involucao", memcmp(mixed, d, 32) == 0);

    /* --- hex --- */
    hsv3_hex32(zeros, hex);
    eq_str("hex32 zeros", hex,
           "0000000000000000000000000000000000000000000000000000000000000000");
    hsv3_hex32(ones, hex);
    eq_str("hex32 0xff", hex,
           "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    hsv3_phys_wipe(&p);
    hsv3_chip_wipe(&c);
}

static void test_rejects(void)
{
    uint8_t ent[32], out[32];
    size_t out_len;
    char mn[HSV3_MNEMONIC_MAX];

    memset(ent, 0, sizeof(ent));

    /* comprimentos de entropia invalidos */
    ok("entropia 15 bytes recusada",
       hsv3_bip39_from_entropy(ent, 15, mn, sizeof(mn)) == HSV3_ERR_ARG);
    ok("entropia 33 bytes recusada",
       hsv3_bip39_from_entropy(ent, 33, mn, sizeof(mn)) == HSV3_ERR_ARG);
    ok("entropia 0 bytes recusada",
       hsv3_bip39_from_entropy(ent, 0, mn, sizeof(mn)) == HSV3_ERR_ARG);

    /* buffer pequeno nao pode transbordar */
    ok("buffer curto recusado",
       hsv3_bip39_from_entropy(ent, 32, mn, 10) == HSV3_ERR_RANGE);

    /* checksum errado: trocar a ultima palavra de "...about" para "...abandon" */
    ok("checksum invalido recusado",
       hsv3_bip39_to_entropy("abandon abandon abandon abandon abandon abandon "
                             "abandon abandon abandon abandon abandon abandon",
                             out, sizeof(out), &out_len) == HSV3_ERR_RANGE);
    /* "bitcoin" nao pertence a wordlist BIP39 -- ao contrario de "banana" e
     * "satoshi", que pertencem. */
    ok("palavra desconhecida recusada",
       hsv3_bip39_to_entropy("abandon abandon abandon abandon abandon abandon "
                             "abandon abandon abandon abandon abandon bitcoin",
                             out, sizeof(out), &out_len) == HSV3_ERR_ARG);
    ok("11 palavras recusadas",
       hsv3_bip39_to_entropy("abandon abandon abandon abandon abandon abandon "
                             "abandon abandon abandon abandon abandon",
                             out, sizeof(out), &out_len) == HSV3_ERR_ARG);

    /* passphrase: so ASCII imprimivel */
    ok("passphrase vazia ok", hsv3_bip39_passphrase_valid("") == HSV3_OK);
    ok("passphrase ascii ok", hsv3_bip39_passphrase_valid("Correct Horse 42!") == HSV3_OK);
    ok("passphrase com tab recusada", hsv3_bip39_passphrase_valid("a\tb") == HSV3_ERR_ARG);
    ok("passphrase com \\n recusada", hsv3_bip39_passphrase_valid("a\nb") == HSV3_ERR_ARG);
    ok("passphrase nao-ascii recusada",
       hsv3_bip39_passphrase_valid("caf\xc3\xa9") == HSV3_ERR_ARG);

    /* caminhos de derivacao mal formados */
    {
        uint8_t seed[64];
        hsv3_hdkey_t root, node;
        memset(seed, 7, sizeof(seed));
        hsv3_bip32_master(seed, 64, &root);
        ok("caminho 'm/' recusado", hsv3_bip32_derive(&root, "m/", &node) == HSV3_ERR_ARG);
        ok("caminho 'm//0' recusado", hsv3_bip32_derive(&root, "m//0", &node) == HSV3_ERR_ARG);
        ok("caminho 'm/x' recusado", hsv3_bip32_derive(&root, "m/x", &node) == HSV3_ERR_ARG);
        ok("caminho com indice enorme recusado",
           hsv3_bip32_derive(&root, "m/4294967296", &node) == HSV3_ERR_ARG);
        ok("caminho 'm' ok (identidade)", hsv3_bip32_derive(&root, "m", &node) == HSV3_OK);
        hsv3_hdkey_wipe(&node);
        hsv3_hdkey_wipe(&root);
    }
}

int main(void)
{
    printf("HELTEC-SEED-V3 -- testes do nucleo (C, backend mbedTLS)\n\n");

    test_wordlist();
    test_bip39();
    test_bip32();
    test_bip84();
    test_entropy();
    test_rejects();

    printf("\n");
    if (fails) {
        printf("FALHOU: %d de %d verificacoes\n", fails, checks);
        return 1;
    }
    printf("OK -- todas as %d verificacoes passaram\n", checks);
    printf("     %d vetores BIP39 oficiais, %d caminhos BIP32 oficiais,\n",
           BIP39_VEC_COUNT, BIP32_VEC_COUNT);
    printf("     BIP84 (zpub + 3 enderecos), entropia fail-closed e recusas.\n");
    return 0;
}
