/* Corre a cerimonia no PC e imprime o resultado em formato legivel por maquina.
 *
 * Existe para uma coisa so: permitir que tools/crosscheck.py compare, campo a
 * campo, o que o nucleo em C produz contra o que o verify.py (implementacao
 * independente, em Python) produz para a mesma entrada. Se as duas concordarem
 * em milhares de entradas aleatorias, e porque nenhuma tem um bug que a outra
 * nao tenha -- que e a garantia mais forte que se consegue sem uma auditoria.
 *
 *   hsv3_dump --dice <digitos> --chip <64 hex> [--passphrase <ascii>]
 */
#include <stdio.h>
#include <string.h>

#include "hsv3_bech32.h"
#include "hsv3_bip32.h"
#include "hsv3_bip39.h"
#include "hsv3_crypto.h"
#include "hsv3_entropy.h"

static const struct { const char *label, *path; uint32_t ver; } PATHS[] = {
    { "bip84", "m/84'/0'/0'", HSV3_VER_ZPUB },
    { "bip86", "m/86'/0'/0'", HSV3_VER_XPUB },
    { "bip49", "m/49'/0'/0'", HSV3_VER_YPUB },
    { "bip44", "m/44'/0'/0'", HSV3_VER_XPUB },
};

int main(int argc, char **argv)
{
    const char *dice = NULL, *coin = NULL, *chip_hex = NULL, *passphrase = "";
    hsv3_phys_t phys;
    hsv3_chip_t chip;
    uint8_t e_phys[32], e_chip[32], commit[32], entropy[32], seed[64], fp[4];
    char mn[HSV3_MNEMONIC_MAX], hex[65], xkey[HSV3_XKEY_MAX], addr[HSV3_ADDR_MAX];
    hsv3_hdkey_t master, acct, node;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dice") && i + 1 < argc)            dice = argv[++i];
        else if (!strcmp(argv[i], "--coin") && i + 1 < argc)       coin = argv[++i];
        else if (!strcmp(argv[i], "--chip") && i + 1 < argc)       chip_hex = argv[++i];
        else if (!strcmp(argv[i], "--passphrase") && i + 1 < argc) passphrase = argv[++i];
        else { fprintf(stderr, "argumento desconhecido: %s\n", argv[i]); return 2; }
    }
    if ((dice == NULL) == (coin == NULL) || chip_hex == NULL) {
        fprintf(stderr, "uso: hsv3_dump (--dice D | --coin D) --chip HEX "
                        "[--passphrase P]\n");
        return 2;
    }
    if (strlen(chip_hex) != 64) { fprintf(stderr, "--chip precisa de 64 hex\n"); return 2; }

    hsv3_phys_init(&phys, dice ? HSV3_PHYS_DICE : HSV3_PHYS_COIN);
    for (const char *p = dice ? dice : coin; *p; p++) {
        if (hsv3_phys_push(&phys, *p) != HSV3_OK) {
            fprintf(stderr, "digito invalido: %c\n", *p);
            return 2;
        }
    }
    if (hsv3_phys_digest(&phys, e_phys) != HSV3_OK) {
        fprintf(stderr, "lancamentos insuficientes\n");
        return 2;
    }

    /* O firmware preenche as tres fontes com hardware; aqui basta reproduzir o
     * E_chip que o ecra mostrou, por isso injeta-se ja o valor final. */
    for (int i = 0; i < 32; i++) {
        unsigned v;
        sscanf(chip_hex + i * 2, "%2x", &v);
        e_chip[i] = (uint8_t)v;
    }
    hsv3_chip_init(&chip);

    if (hsv3_entropy_commit(e_chip, commit) != HSV3_OK) return 1;
    if (hsv3_entropy_combine(e_phys, e_chip, entropy) != HSV3_OK) return 1;
    if (hsv3_bip39_from_entropy(entropy, 32, mn, sizeof(mn)) != HSV3_OK) return 1;
    if (hsv3_bip39_seed(mn, passphrase, seed) != HSV3_OK) return 1;
    if (hsv3_bip32_master(seed, 64, &master) != HSV3_OK) return 1;
    hsv3_bip32_fingerprint(&master, fp);

    hsv3_hex32(e_phys, hex);  printf("e_phys %s\n", hex);
    hsv3_hex32(e_chip, hex);  printf("e_chip %s\n", hex);
    hsv3_hex32(commit, hex);  printf("commit %s\n", hex);
    hsv3_hex32(entropy, hex); printf("entropy %s\n", hex);
    printf("mnemonic %s\n", mn);
    printf("fingerprint %02x%02x%02x%02x\n", fp[0], fp[1], fp[2], fp[3]);

    for (size_t i = 0; i < sizeof(PATHS) / sizeof(PATHS[0]); i++) {
        if (hsv3_bip32_derive(&master, PATHS[i].path, &acct) != HSV3_OK) return 1;
        if (hsv3_bip32_serialize(&acct, PATHS[i].ver, 0, xkey, sizeof(xkey)) != HSV3_OK) return 1;
        printf("%s %s\n", PATHS[i].label, xkey);
    }
    if (hsv3_bip32_derive(&master, "m/84'/0'/0'/0/0", &node) != HSV3_OK) return 1;
    if (hsv3_address_p2wpkh(node.pub, addr, sizeof(addr)) != HSV3_OK) return 1;
    printf("addr0 %s\n", addr);

    hsv3_hdkey_wipe(&node);
    hsv3_hdkey_wipe(&acct);
    hsv3_hdkey_wipe(&master);
    hsv3_wipe(seed, sizeof(seed));
    hsv3_wipe(commit, sizeof(commit));
    hsv3_wipe(entropy, sizeof(entropy));
    hsv3_wipe(mn, sizeof(mn));
    hsv3_phys_wipe(&phys);
    hsv3_chip_wipe(&chip);
    return 0;
}
