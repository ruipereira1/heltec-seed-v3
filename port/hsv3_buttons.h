/* O botao. Um so: o PRG (GPIO0), que a placa ja tem.
 *
 * Dois gestos, e mais nenhum: tocar e manter. Nao ha terceiro evento nem
 * duracao intermedia que o utilizador tenha de acertar -- qualquer pressao a
 * partir de 350 ms escolhe, dure meio segundo ou cinco.
 *
 * Cada flanco alimenta hsv3_rng_timing_feed() com a contagem de ciclos: os
 * intervalos entre toques sao uma das tres fontes de entropia.
 */
#ifndef HSV3_BUTTONS_H
#define HSV3_BUTTONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Classificacao exaustiva: qualquer duracao cai numa das duas. Nenhum toque
 * pode ser descartado sem o utilizador ver reaccao nenhuma. */
typedef enum {
    HSV3_BTN_NONE = 0,
    HSV3_BTN_SHORT,      /* PRG < 350 ms   -> avanca / muda de opcao */
    HSV3_BTN_LONG        /* PRG >= 350 ms  -> escolhe / confirma */
} hsv3_btn_t;

/* Configura o GPIO do PRG com pull-up. */
void hsv3_buttons_init(void);

/* Bloqueia ate haver um evento, ou devolve HSV3_BTN_NONE ao fim de timeout_ms
 * (timeout_ms < 0 espera para sempre). */
hsv3_btn_t hsv3_buttons_wait(int timeout_ms);

/* Duracao, em ms, do ultimo toque no PRG que foi classificado. Existe para o
 * ecra de diagnostico: sem isto, quando um toque "nao faz nada", nao ha forma
 * de saber se o problema esta na medicao, na classificacao, ou se a placa
 * reiniciou sem deixar rasto. */
int hsv3_buttons_last_ms(void);

/* Escolhe uma de n opcoes com um botao so.
 *
 *   toque rapido      avanca uma posicao (controlo fino)
 *   manter carregado  as opcoes passam sozinhas, ~2.5 por segundo
 *   largar            escolhe a que estiver a mostrar
 *
 * Um gesto por escolha, em vez de "andar ate la e depois confirmar". E nao ha
 * nenhuma duracao que o utilizador tenha de acertar: olha para o ecra e larga.
 * Medido na placa, os toques humanos dao ~170 ms ou mais de 1500 ms -- qualquer
 * desenho que dependa de acertar num intervalo intermedio nao funciona.
 *
 * redraw(idx) e chamado sempre que a opcao visivel muda, para o chamador
 * desenhar o ecra. Devolve o indice escolhido.
 */
int hsv3_buttons_select(int n_options, int start, void (*redraw)(int idx));

/* Conta toques ate haver uma pausa de pause_ms sem nenhum, e devolve quantos.
 *
 * E' o unico gesto que existe: tocar. Para um dado que saiu 4, toca-se 4 vezes
 * e para-se -- o valor grava-se sozinho. Nao ha nada para segurar, nada para
 * confirmar, e nenhuma duracao para acertar.
 *
 * redraw(taps, pct) e chamado a cada toque (pct=0) e durante a pausa, com a
 * percentagem ja decorrida, para se poder mostrar a barra de gravacao. */
int hsv3_buttons_count_taps(int pause_ms, void (*redraw)(int taps, int pct));

#ifdef __cplusplus
}
#endif

#endif /* HSV3_BUTTONS_H */
