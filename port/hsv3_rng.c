/* Entropia do lado do chip, no ESP32-S3.
 *
 * O DETALHE QUE MATOU A COLDCARD MK3
 * ----------------------------------
 * No ESP32-S3, esp_random() so devolve numeros verdadeiramente aleatorios se
 * o subsistema de RF estiver ligado (Wi-Fi ou Bluetooth), OU se a fonte interna
 * de entropia tiver sido ligada com bootloader_random_enable(). Fora disso
 * devolve pseudo-aleatorios -- e devolve-os sem se queixar.
 *
 * Isso e exatamente a forma do bug da Mk3: uma funcao que parecia dar entropia
 * de hardware e que, em silencio, dava saida de um PRNG. Aqui nao ligamos radio
 * nenhum (e uma maquina de gerar seeds), por isso bootloader_random_enable() e
 * obrigatorio e o seu sucesso e verificado antes de qualquer geracao.
 *
 * A regra deste ficheiro: em caso de duvida, FALHA. Nunca devolve entropia de
 * qualidade inferior a prometida.
 */

#include "hsv3_rng.h"

#include <string.h>

#include "bootloader_random.h"
#include "esp_cpu.h"
#include "soc/rtc.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hsv3_crypto.h"

static int s_rng_ready = 0;

/* --- TRNG -------------------------------------------------------------- */

/* Callback no formato que o mbedTLS espera, para mascarar a multiplicacao
 * escalar. Nao gera chaves -- so mascara. */
static int esp_rng_for_blinding(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    if (!s_rng_ready) return -1;
    esp_fill_random(buf, len);
    return 0;
}

int hsv3_rng_init(void)
{
    uint32_t a, b;

    s_rng_ready = 0;

    /* Tem de vir antes de qualquer inicializacao de ADC: a fonte interna usa o
     * SAR ADC a amostrar ruido da tensao de referencia. */
    bootloader_random_enable();

    /* Nao ha registo que diga "estou ligado", por isso testa-se o efeito.
     * Isto NAO prova que a saida e boa -- a saida do Yasmarang tambem passaria
     * aqui. Prova apenas que a fonte nao esta morta ou presa. Contra RNG
     * comprometido, o que protege e o XOR com os dados fisicos. */
    a = esp_random();
    for (int i = 0; i < 8; i++) {
        b = esp_random();
        if (b != a) {
            s_rng_ready = 1;
            break;
        }
        a = b;
    }
    if (!s_rng_ready) return HSV3_ERR_BACKEND;

    /* Instala o TRNG do chip como mascara da multiplicacao escalar do mbedTLS
     * (blinding contra analise temporal). Passar NULL aqui deixaria o backend
     * a usar a mascara determinista, que so serve para os testes no PC. */
    hsv3_ec_set_rng(esp_rng_for_blinding, NULL);
    return HSV3_OK;
}

int hsv3_rng_trng(uint8_t out[32])
{
    if (!s_rng_ready) return HSV3_ERR_BACKEND;
    esp_fill_random(out, 32);
    return HSV3_OK;
}

/* --- jitter do oscilador ----------------------------------------------- */
/*
 * Conta quantos ciclos da CPU -- presa ao cristal de 40 MHz -- passam enquanto
 * o RTC_SLOW_CLK avanca alguns tiques. Esse relogio corre no RC interno de
 * ~136 kHz (CONFIG_RTC_CLK_SRC_INT_RC), que e' um oscilador FISICAMENTE
 * SEPARADO do cristal. Os dois derivam um do outro por ruido termico, e e' dai
 * que vem a imprevisibilidade dos bits de baixo da contagem.
 *
 * O QUE ISTO ERA ANTES, E PORQUE ESTAVA ERRADO
 * --------------------------------------------
 * A primeira versao media contra esp_timer_get_time(). O comentario dizia o
 * mesmo que este -- "RC contra cristal" -- mas nao era verdade: no ESP32-S3 o
 * esp_timer corre em cima do systimer, e
 *
 *     clk_tree_defs.h:  SYSTIMER_CLK_SRC_XTAL = SOC_MOD_CLK_XTAL
 *                       SYSTIMER_CLK_SRC_DEFAULT = SOC_MOD_CLK_XTAL
 *
 * ou seja, o systimer e' alimentado pelo mesmo cristal que a CPU. Nao havia
 * dois osciladores nenhuns: eram dois contadores do mesmo relogio. O que sobrava
 * era ruido de temporizacao de software -- quantizacao do laco, cache, e o tique
 * do FreeRTOS a 1 kHz a cair dentro de uma amostra em cada 33, que e' uma
 * estrutura periodica e portanto previsivel.
 *
 * Com rtc_time_get() a medicao passa a ser mesmo entre dois dominios de relogio.
 *
 * Recolhe-se 1 bit por amostra (paridade da contagem) e passa-se tudo por SHA256
 * no fim. Quanta entropia ha por amostra, so medindo: tools/entropia.py.
 */

/* Um tique do RC lento sao ~7.35 us a 136 kHz; quatro dao ~29 us, a mesma ordem
 * de grandeza da janela anterior. Menos do que isto e a leitura do contador
 * passa a pesar mais do que a espera. */
#define JITTER_RTC_TICKS 4

/* JITTER_SAMPLES sai da min-entropia assumida por amostra:
 *
 *     amostras = 256 bits desejados / H por amostra
 *
 * Com H = 1/2 dao 512, e recolhem-se 2048 -- quatro vezes mais, para haver
 * margem se a medicao real vier abaixo do assumido. Nao e' um numero redondo
 * escolhido a olho: se mudares o HSV3_JITTER_H_MIN, muda com ele. */
#define JITTER_SAMPLES \
    (256 * HSV3_JITTER_H_MIN_DEN / HSV3_JITTER_H_MIN_NUM * 4)

/* --- testes de saude (SP 800-90B 4.4) ----------------------------------- */
/*
 * O teste antigo -- "as contagens variaram pelo menos 1 em 8 vezes" -- so
 * apanhava uma fonte completamente morta. Uma fonte a debitar 0101010101...
 * passava, e uma fonte que degradasse a meio da recolha passava tambem.
 *
 * Estes dois sao os do NIST, correm DURANTE a recolha, e tem cortes calculados
 * para uma taxa de falso alarme de 2^-20: cerca de uma cerimonia em um milhao
 * aborta sem razao. Abortar sem razao e' chato; continuar com uma fonte presa
 * e' o bug da Coldcard Mk3.
 */

void hsv3_health_init(hsv3_health_t *h)
{
    memset(h, 0, sizeof(*h));
}

int hsv3_health_feed(hsv3_health_t *h, uint8_t amostra)
{
    /* --- RCT: quantas amostras iguais seguidas --- */
    if (!h->arrancou) {
        h->arrancou = 1;
        h->anterior = amostra;
        h->repeticoes = 1;
    } else if (amostra == h->anterior) {
        h->repeticoes++;
        if (h->repeticoes >= HSV3_RCT_CUTOFF) return HSV3_ERR_BACKEND;
    } else {
        h->anterior = amostra;
        h->repeticoes = 1;
    }

    /* --- APT: quantas iguais a primeira, numa janela de 1024 --- */
    if (h->na_janela == 0) {
        h->ref = amostra;
        h->iguais = 1;
        h->na_janela = 1;
    } else {
        if (amostra == h->ref) h->iguais++;
        h->na_janela++;
        if (h->iguais > HSV3_APT_CUTOFF) return HSV3_ERR_BACKEND;
        if (h->na_janela >= HSV3_APT_WINDOW) h->na_janela = 0;   /* nova janela */
    }
    return HSV3_OK;
}

/* Uma amostra: quantos ciclos da CPU passam enquanto o RC bate JITTER_RTC_TICKS
 * vezes. A espera termina no ritmo do RC; a contagem corre no ritmo do cristal. */
static uint32_t jitter_sample(void)
{
    uint64_t alvo = rtc_time_get() + JITTER_RTC_TICKS;
    uint32_t inicio = esp_cpu_get_cycle_count();
    while (rtc_time_get() < alvo) {
        /* espera activa: o fim depende de quando o RC bate */
    }
    return esp_cpu_get_cycle_count() - inicio;
}

int hsv3_rng_jitter(uint8_t out[32])
{
    uint8_t acc[JITTER_SAMPLES / 8];
    hsv3_health_t saude;

    memset(acc, 0, sizeof(acc));
    hsv3_health_init(&saude);

    for (int i = 0; i < JITTER_SAMPLES; i++) {
        uint8_t bit = (uint8_t)(jitter_sample() & 1u);

        /* Falhar aqui para a cerimonia. Nao ha recolha parcial aproveitada. */
        if (hsv3_health_feed(&saude, bit) != HSV3_OK) {
            hsv3_wipe(acc, sizeof(acc));
            hsv3_wipe(&saude, sizeof(saude));
            return HSV3_ERR_BACKEND;
        }
        acc[i / 8] |= (uint8_t)(bit << (i % 8));
    }

    hsv3_sha256(acc, sizeof(acc), out);
    hsv3_wipe(acc, sizeof(acc));
    hsv3_wipe(&saude, sizeof(saude));
    return HSV3_OK;
}

#ifdef HSV3_JITTER_DUMP
void hsv3_rng_jitter_raw(uint32_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) out[i] = jitter_sample();
}
#endif

/* --- timing humano ------------------------------------------------------ */
/*
 * Os intervalos entre toques do utilizador, medidos em ciclos de CPU, sao
 * imprevisiveis ao nivel dos bits baixos. Sao de graca: a cerimonia ja obriga a
 * centenas de toques para introduzir os lancamentos.
 */

static uint8_t  s_timing_pool[64];
static size_t   s_timing_pos = 0;
static unsigned s_timing_events = 0;

void hsv3_rng_timing_feed(uint32_t cycle_count)
{
    /* Mistura por XOR num pool circular, para nao ter de guardar tudo. */
    for (int i = 0; i < 4; i++) {
        s_timing_pool[s_timing_pos] ^= (uint8_t)(cycle_count >> (i * 8));
        s_timing_pos = (s_timing_pos + 1) % sizeof(s_timing_pool);
    }
    if (s_timing_events < 0xffffffffu) s_timing_events++;
}

unsigned hsv3_rng_timing_count(void)
{
    return s_timing_events;
}

/* Nao apaga o pool: e' chamada uma vez por ronda de entropia (seed e, se for
 * o caso, passphrase), e os toques da ronda anterior continuam a ser entropia
 * valida para a seguinte. Quem apaga e' hsv3_rng_timing_wipe(), no fim. */
int hsv3_rng_timing_finish(uint8_t out[32])
{
    if (s_timing_events < HSV3_TIMING_MIN_EVENTS) return HSV3_ERR_RANGE;
    hsv3_sha256(s_timing_pool, sizeof(s_timing_pool), out);
    return HSV3_OK;
}

void hsv3_rng_timing_wipe(void)
{
    hsv3_wipe(s_timing_pool, sizeof(s_timing_pool));
    s_timing_pos = 0;
    s_timing_events = 0;
}
