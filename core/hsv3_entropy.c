#include "hsv3_entropy.h"

#include <string.h>

#include "hsv3_crypto.h"

void hsv3_phys_init(hsv3_phys_t *p, hsv3_phys_kind_t kind)
{
    memset(p, 0, sizeof(*p));
    p->kind = kind;
}

static int digit_ok(hsv3_phys_kind_t kind, char d)
{
    if (kind == HSV3_PHYS_DICE) return d >= '1' && d <= '6';
    return d == '0' || d == '1';
}

int hsv3_phys_push(hsv3_phys_t *p, char digit)
{
    if (!digit_ok(p->kind, digit)) return HSV3_ERR_ARG;
    if (p->count >= HSV3_PHYS_MAX) return HSV3_ERR_RANGE;
    p->digits[p->count++] = digit;
    p->digits[p->count] = '\0';
    return HSV3_OK;
}

int hsv3_phys_pop(hsv3_phys_t *p)
{
    if (p->count == 0) return HSV3_ERR_RANGE;
    p->digits[--p->count] = '\0';
    return HSV3_OK;
}

size_t hsv3_phys_min(const hsv3_phys_t *p)
{
    return p->kind == HSV3_PHYS_DICE ? HSV3_DICE_MIN_ROLLS : HSV3_COIN_MIN_FLIPS;
}

int hsv3_phys_enough(const hsv3_phys_t *p)
{
    return p->count >= hsv3_phys_min(p);
}

void hsv3_phys_wipe(hsv3_phys_t *p)
{
    hsv3_wipe(p, sizeof(*p));
}

int hsv3_phys_digest(const hsv3_phys_t *p, uint8_t out[32])
{
    /* Recusa em vez de degradar. Ver o cabecalho deste modulo. */
    if (!hsv3_phys_enough(p)) return HSV3_ERR_RANGE;
    for (size_t i = 0; i < p->count; i++) {
        if (!digit_ok(p->kind, p->digits[i])) return HSV3_ERR_ARG;
    }
    hsv3_sha256((const uint8_t *)p->digits, p->count, out);
    return HSV3_OK;
}

/* --- lado do chip ----------------------------------------------------- */

void hsv3_chip_init(hsv3_chip_t *c)
{
    memset(c, 0, sizeof(*c));
}

void hsv3_chip_set(hsv3_chip_t *c, unsigned which, const uint8_t value[32])
{
    uint8_t *dst = NULL;
    switch (which) {
    case HSV3_SRC_TRNG:   dst = c->trng;   break;
    case HSV3_SRC_JITTER: dst = c->jitter; break;
    case HSV3_SRC_TIMING: dst = c->timing; break;
    default: return;
    }
    memcpy(dst, value, 32);
    c->present |= which;
}

void hsv3_chip_wipe(hsv3_chip_t *c)
{
    hsv3_wipe(c, sizeof(*c));
}

/* Fonte obviamente morta.
 *
 * A versao anterior so via "todos os bytes iguais", o que apanha 0x00 e 0xff
 * mas deixa passar 0x5a5a5a5a... ou qualquer padrao curto a repetir -- que e'
 * exatamente o aspecto de um registo preso ou de um barramento a devolver
 * sempre a mesma palavra.
 *
 * Agora procura periodicidade em 1, 2, 4, 8 e 16 bytes. Continua a NAO apanhar
 * um PRNG fraco: a saida do Yasmarang que matou a Mk3 passaria aqui na mesma,
 * e nenhum teste local a apanharia. Contra isso o que protege e' o XOR com os
 * dados fisicos e o compromisso -- nao esta funcao. */
static int looks_dead(const uint8_t b[32])
{
    static const size_t periodos[] = { 1, 2, 4, 8, 16 };

    for (size_t p = 0; p < sizeof(periodos) / sizeof(periodos[0]); p++) {
        size_t n = periodos[p];
        size_t i;
        for (i = n; i < 32; i++) {
            if (b[i] != b[i - n]) break;
        }
        if (i == 32) return 1;   /* repetiu-se ate ao fim */
    }
    return 0;
}

int hsv3_chip_digest(const hsv3_chip_t *c, uint8_t out[32])
{
    uint8_t buf[96];

    if ((c->present & HSV3_SRC_ALL) != HSV3_SRC_ALL) return HSV3_ERR_RANGE;
    if (looks_dead(c->trng) || looks_dead(c->jitter) || looks_dead(c->timing)) {
        return HSV3_ERR_RANGE;
    }

    memcpy(buf, c->trng, 32);
    memcpy(buf + 32, c->jitter, 32);
    memcpy(buf + 64, c->timing, 32);
    hsv3_sha256(buf, sizeof(buf), out);
    hsv3_wipe(buf, sizeof(buf));
    return HSV3_OK;
}

/* SHA256 simples, sem etiqueta de dominio nem truncagem: assim o compromisso
 * pode ser conferido com um `sha256sum` qualquer, sem depender deste projeto.
 * Ver o cabecalho para o ataque que isto fecha e para o que fica de fora. */
int hsv3_entropy_commit(const uint8_t chip[32], uint8_t out[32])
{
    if (chip == NULL || out == NULL) return HSV3_ERR_ARG;
    hsv3_sha256(chip, 32, out);
    return HSV3_OK;
}

int hsv3_entropy_combine(const uint8_t phys[32], const uint8_t chip[32],
                         uint8_t out[32])
{
    if (phys == NULL || chip == NULL || out == NULL) return HSV3_ERR_ARG;
    for (size_t i = 0; i < 32; i++) {
        out[i] = (uint8_t)(phys[i] ^ chip[i]);
    }
    return HSV3_OK;
}

void hsv3_hex32(const uint8_t in[32], char out[65])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; i++) {
        out[i * 2]     = digits[in[i] >> 4];
        out[i * 2 + 1] = digits[in[i] & 0x0f];
    }
    out[64] = '\0';
}
