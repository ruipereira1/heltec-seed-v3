/* Recolha e combinacao de entropia.
 *
 * Este e o ficheiro mais importante do projeto. O bug que destruiu a Coldcard
 * Mk3 em julho de 2026 viveu exatamente aqui: uma funcao de entropia com um
 * caminho alternativo silencioso, que caiu num PRNG fraco sem ninguem dar por
 * isso durante cinco anos.
 *
 * Por isso, quatro regras que nao se negoceiam neste ficheiro:
 *
 *   1. NAO HA FALLBACK. Se uma fonte nao puder ser provada activa, a funcao
 *      devolve erro. Nunca devolve entropia degradada.
 *   2. A entropia fisica do utilizador entra sempre, por XOR. Mesmo que todo o
 *      silicio esteja comprometido, os dados lancados a mao salvam a seed.
 *   3. O chip compromete-se com a sua metade ANTES de ver os lancamentos, e o
 *      compromisso e' anotado antes de o primeiro dado rolar. Sem isso, a regra
 *      2 e' falsa: ver hsv3_entropy_commit() para o ataque concreto.
 *   4. As duas metades sao mostradas no ecra, para poderem ser reproduzidas em
 *      tools/verify.py. Sem verificabilidade, o resto nao vale nada.
 */
#ifndef HSV3_ENTROPY_H
#define HSV3_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* log2(6) * 100 = 258.5 bits. Com 99 ficariam 255.9 -- abaixo de 256. */
#define HSV3_DICE_MIN_ROLLS  100
#define HSV3_COIN_MIN_FLIPS  256
#define HSV3_PHYS_MAX        512   /* deixa margem para quem quiser lancar mais */

typedef enum {
    HSV3_PHYS_DICE = 0,   /* digitos '1'..'6' */
    HSV3_PHYS_COIN = 1    /* digitos '0'..'1' */
} hsv3_phys_kind_t;

/* Acumulador dos lancamentos. Fica em SRAM e e apagado no fim da cerimonia. */
typedef struct {
    hsv3_phys_kind_t kind;
    size_t           count;
    char             digits[HSV3_PHYS_MAX + 1];
} hsv3_phys_t;

void   hsv3_phys_init(hsv3_phys_t *p, hsv3_phys_kind_t kind);
int    hsv3_phys_push(hsv3_phys_t *p, char digit);   /* HSV3_OK ou HSV3_ERR_ARG */
int    hsv3_phys_pop(hsv3_phys_t *p);                /* desfaz o ultimo */
size_t hsv3_phys_min(const hsv3_phys_t *p);          /* quantos sao precisos */
int    hsv3_phys_enough(const hsv3_phys_t *p);       /* 1 se ja chegam */
void   hsv3_phys_wipe(hsv3_phys_t *p);

/* SHA256 dos digitos em ASCII -- a mesma convencao da Coldcard e do SeedSigner,
 * para o resultado poder ser reproduzido por ferramentas de terceiros.
 * Recusa (HSV3_ERR_RANGE) se ainda nao houver lancamentos suficientes. */
int hsv3_phys_digest(const hsv3_phys_t *p, uint8_t out[32]);

/* --- lado do chip ----------------------------------------------------- */

#define HSV3_SRC_TRNG    0x01u   /* esp_random() apos bootloader_random_enable() */
#define HSV3_SRC_JITTER  0x02u   /* desvio do RC interno contra o cristal */
#define HSV3_SRC_TIMING  0x04u   /* contagem de ciclos nos flancos dos botoes */
#define HSV3_SRC_ALL     (HSV3_SRC_TRNG | HSV3_SRC_JITTER | HSV3_SRC_TIMING)

typedef struct {
    uint8_t  trng[32];
    uint8_t  jitter[32];
    uint8_t  timing[32];
    unsigned present;   /* mascara HSV3_SRC_* */
} hsv3_chip_t;

void hsv3_chip_init(hsv3_chip_t *c);
void hsv3_chip_set(hsv3_chip_t *c, unsigned which, const uint8_t value[32]);
void hsv3_chip_wipe(hsv3_chip_t *c);

/* SHA256(trng || jitter || timing).
 *
 * Devolve HSV3_ERR_RANGE se faltar alguma fonte, ou se alguma vier obviamente
 * morta (tudo a zeros, tudo a 0xff, ou todos os bytes iguais).
 *
 * Nota honesta sobre esse teste: apanha uma fonte desligada ou em curto, mas
 * NAO teria apanhado o bug da Mk3 -- a saida do Yasmarang parece aleatoria e
 * passaria aqui sem problema. Contra esse tipo de falha, o que protege e o XOR
 * com os dados fisicos e a verificacao em tools/verify.py. */
int hsv3_chip_digest(const hsv3_chip_t *c, uint8_t out[32]);

/* Compromisso do chip: C = SHA256(E_chip). Mostrado no ecra ANTES do primeiro
 * lancamento, e anotado pelo utilizador antes de mexer nos dados.
 *
 * PORQUE E' QUE ISTO E' PRECISO
 * -----------------------------
 * Sem compromisso, o XOR nao protege contra um chip malicioso. Se o dispositivo
 * so fecha o E_chip DEPOIS de ver os lancamentos, um TRNG comprometido escolhe
 *
 *     E_chip = E_fisica XOR seed_que_o_atacante_quer
 *
 * e a cerimonia inteira fica consistente: os dois hex do ecra batem certo, o
 * tools/verify.py recalcula, diz OK, e a seed e' do atacante. O verify.py so
 * prova que a aritmetica esta certa -- nao prova que o chip contribuiu de forma
 * honesta.
 *
 * Com o compromisso anotado antes de rolar o primeiro dado, o chip fica preso a
 * um E_chip escolhido quando ainda nao podia conhecer os lancamentos. Mudar de
 * ideias no fim exigiria uma segunda preimagem de SHA256.
 *
 * O QUE ISTO CONTINUA A NAO RESOLVER
 * ----------------------------------
 * Um dispositivo malicioso pode sempre recusar-se a acabar e obrigar a repetir
 * a cerimonia ate calhar um resultado que lhe sirva. Isso custa-lhe uma ronda
 * inteira de lancamentos por cada bit que consegue enviesar, e da nas vistas.
 * Se a placa abortar repetidamente sem razao, deita-a fora. */
int hsv3_entropy_commit(const uint8_t chip[32], uint8_t out[32]);

/* out = phys XOR chip. E aqui que as duas metades se juntam. */
int hsv3_entropy_combine(const uint8_t phys[32], const uint8_t chip[32],
                         uint8_t out[32]);

/* Converte 32 bytes em 64 caracteres hex minusculos, com terminador. */
void hsv3_hex32(const uint8_t in[32], char out[65]);

#ifdef __cplusplus
}
#endif

#endif /* HSV3_ENTROPY_H */
