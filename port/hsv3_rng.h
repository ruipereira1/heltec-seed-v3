/* Fontes de entropia do ESP32-S3. Ver hsv3_rng.c para o porque de cada uma. */
#ifndef HSV3_RNG_H
#define HSV3_RNG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Liga a fonte interna de entropia e prova que ficou ligada.
 * Tem de ser chamada uma vez no arranque, ANTES de qualquer uso do ADC.
 * Se devolver != HSV3_OK, a cerimonia nao pode continuar. */
int hsv3_rng_init(void);

/* 32 bytes do TRNG do chip. Falha se hsv3_rng_init() nao tiver corrido bem. */
int hsv3_rng_trng(uint8_t out[32]);

/* --- fonte de jitter ---------------------------------------------------- */
/*
 * MIN-ENTROPIA ASSUMIDA POR AMOSTRA.
 *
 * Este numero e' uma suposicao conservadora, NAO uma medicao. Enquanto assim
 * for, e' a afirmacao mais fraca do projeto -- e esta aqui em vez de estar
 * escondida num comentario para que se veja.
 *
 * Para o medir na tua placa:
 *
 *     idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug" \
 *            -D HSV3_JITTER_DUMP=1 build flash monitor > amostras.txt
 *     python tools/entropia.py amostras.txt
 *
 * Se a medicao der menos do que isto, ha duas saidas honestas: subir o
 * JITTER_SAMPLES na proporcao, ou baixar este valor e aceitar que a fonte
 * contribui menos do que se dizia. O que nao se pode e' deixar ficar.
 */
#define HSV3_JITTER_H_MIN_NUM   1   /* 1 bit por amostra */
#define HSV3_JITTER_H_MIN_DEN   1

/* MEDIDO NESTA PLACA a 04/08/2026, com 4000 amostras:
 *
 *     valores distintos      135
 *     valor mais frequente   15.2%
 *     min-entropia (MCV)     2.72 bits por amostra
 *
 * Assume-se 1 bit, que deixa 2.7x de margem para os estimadores por previsao
 * cortarem abaixo do MCV. Repete a medicao na tua placa antes de confiares
 * neste numero -- o silicio nao e todo igual.
 *
 * A AMOSTRA E' A CONTAGEM INTEIRA, NAO UM BIT DELA
 * ------------------------------------------------
 * A primeira versao guardava so a paridade de cada contagem. A medicao na placa
 * mostrou porque e' que isso nao servia:
 *
 *     bit 0   0.95% de uns   0.014 bits   <- morto
 *     bit 1   55.2%          0.857
 *     bit 4   47.4%          0.928
 *
 * O laco de espera gasta um numero par de ciclos por iteracao, por isso a
 * contagem e' quase sempre par e o bit 0 nao varia. A fonte estava boa; a
 * extraccao e' que deitava fora 31 bits dos 32 e ficava com o unico que estava
 * morto. Na placa isso dava corridas de 245 amostras iguais e o RCT abortava a
 * cerimonia -- com razao.
 *
 * Agora entra a contagem toda no acumulador, e os testes de saude olham para a
 * amostra inteira. Nao depende de nenhum bit em particular estar vivo, o que
 * seria fragil: a granularidade do laco muda com o compilador. */

/* Cortes dos testes de saude do SP 800-90B 4.4, para alfa = 2^-20 e amostras
 * NAO BINARIAS (por isso janela de 512, nao 1024) com a min-entropia acima.
 * Calculados, nao arredondados a olho -- tools/entropia.py --cortes reproduz
 * estes dois numeros. */
#define HSV3_RCT_CUTOFF   21    /* 21 amostras iguais seguidas = fonte presa */
#define HSV3_APT_WINDOW  512
#define HSV3_APT_CUTOFF  311    /* mais de 311 iguais em 512 = enviesada */

/* 32 bytes vindos do desvio entre o RC interno (~136 kHz) e o cristal de
 * 40 MHz. Fonte fisica independente do bloco RNG da Espressif. Demora ~1 s.
 *
 * Falha (HSV3_ERR_BACKEND) se os testes de saude apanharem a fonte presa ou
 * enviesada durante a propria recolha -- que e' quando ninguem esta a olhar. */
int hsv3_rng_jitter(uint8_t out[32]);

/* Os testes de saude, expostos para poderem ser testados no PC com sequencias
 * escolhidas a mao. Devolvem HSV3_OK ou HSV3_ERR_BACKEND.
 *
 * A amostra e' de 16 bits, nao 1: a contagem de ciclos varia numa gama de
 * ~1900 (medido nesta placa), que cabe folgada em 16 bits sem perder nada da
 * variacao. Usar so a paridade era o erro da versao anterior -- ver o
 * historico de HSV3_JITTER_H_MIN mais acima.
 *
 * O estado vive no contexto, nao em estaticas, para os testes poderem correr
 * casos independentes. */
typedef struct {
    int      arrancou;
    uint16_t anterior;      /* RCT: ultima amostra */
    unsigned repeticoes;    /* RCT: quantas iguais seguidas */
    uint16_t ref;           /* APT: a amostra de referencia da janela */
    unsigned na_janela;     /* APT: quantas amostras ja entraram */
    unsigned iguais;        /* APT: quantas iguais a referencia */
} hsv3_health_t;

void hsv3_health_init(hsv3_health_t *h);
int  hsv3_health_feed(hsv3_health_t *h, uint16_t amostra);

#ifdef HSV3_JITTER_DUMP
/* Contagens de ciclos cruas, sem paridade e sem hash: e' o que a medicao de
 * min-entropia precisa de ver. So existe na build de diagnostico -- e essa
 * build nao gera seeds nenhumas, faz so isto. */
void hsv3_rng_jitter_raw(uint32_t *out, size_t n);
#endif

/* Minimo de flancos antes de a fonte de timing poder ser fechada. Cada toque
 * completo (carregar + largar) conta dois. */
#define HSV3_TIMING_MIN_EVENTS 64

/* Alimenta o acumulador de timing com a contagem de ciclos de um flanco de
 * botao. Chamado pelo tratador de botoes a cada toque. */
void hsv3_rng_timing_feed(uint32_t cycle_count);

/* Quantos flancos ja entraram no acumulador.
 *
 * Existe por causa do compromisso (ver core/hsv3_entropy.h): o E_chip tem de
 * ficar fechado ANTES do primeiro lancamento, e o timing e' uma das tres fontes
 * que entram nele. Sem uma contagem visivel nao havia como pedir ao utilizador
 * os toques necessarios antes desse ponto -- e o ecra de aquecimento nao teria
 * barra de progresso nenhuma. */
unsigned hsv3_rng_timing_count(void);

/* Fecha o acumulador de timing. Falha se tiver havido poucos eventos. */
int hsv3_rng_timing_finish(uint8_t out[32]);

/* Apaga o acumulador. Vive em memoria estatica deste modulo, fora da estrutura
 * de segredos do main, por isso tem de ser apagado a mao no fim da cerimonia --
 * senao sobrevive a tudo o resto. */
void hsv3_rng_timing_wipe(void);

#ifdef __cplusplus
}
#endif

#endif /* HSV3_RNG_H */
