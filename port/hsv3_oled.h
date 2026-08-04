/* OLED SSD1306 128x64 do Heltec V3, por I2C.
 *
 * O ecra tem 8 linhas de texto de 21 caracteres (5x7 mais espaco). Chega para
 * 4 palavras por ecra numeradas, e para 32 caracteres hex por linha partidos
 * em dois ecras.
 */
#ifndef HSV3_OLED_H
#define HSV3_OLED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HSV3_OLED_COLS 21   /* 128 / 6 */
#define HSV3_OLED_ROWS  8   /* 64 / 8  */

int  hsv3_oled_init(void);
void hsv3_oled_clear(void);

/* Escreve na linha row (0..7), coluna col (em caracteres). Trunca no fim. */
void hsv3_oled_text(int col, int row, const char *s);

/* Igual, mas com os pixeis invertidos -- serve para destacar avisos. */
void hsv3_oled_text_inv(int col, int row, const char *s);

/* Caractere ampliado, desenhado a pixel. Existe para o valor do dado: e' a
 * unica coisa no ecra que nao pode ser mal lida, por isso nao pode ter o mesmo
 * tamanho que o texto de ajuda. Com scale=3 fica 15x21 pixeis. */
void hsv3_oled_bigchar(int x, int y, char ch, int scale);

/* Uma linha inteira ampliada, para o modo de letras grandes.
 *
 * A escala 2 cada caractere ocupa 10x14 pixeis e avanca 12, por isso cabem 10
 * numa linha em vez de 21. Metade do texto por ecra, mas legivel por quem nao
 * tem vista de aguia -- e o hex e as 24 palavras sao precisamente o que se
 * copia a mao e onde um caractere mal lido custa a carteira.
 *
 * Ao contrario do hsv3_oled_text(), isto desenha a pixel e COMBINA com o que ja
 * la esta, em vez de atribuir o byte da pagina. Pode sobrepor-se a graficos. */
void hsv3_oled_bigtext(int x, int y, const char *s, int scale);

/* Quantos caracteres cabem numa linha a esta escala. */
int hsv3_oled_bigcols(int scale);

/* Linha horizontal, para separar zonas do ecra. */
void hsv3_oled_hline(int x0, int x1, int y, int on);

/* Barra de progresso com moldura, 0..100. */
void hsv3_oled_bar(int x, int y, int w, int h, int pct);

/* Envia o framebuffer para o ecra. Nada aparece sem isto. */
void hsv3_oled_flush(void);

/* Conveniencia: limpa, escreve as linhas dadas (NULL termina) e envia. */
void hsv3_oled_page(const char *lines[]);

/* Contra burn-in: desloca a imagem 1 pixel de forma ciclica. As palavras da
 * seed ficam muito tempo no ecra e podem marcar o painel de forma permanente,
 * o que seria uma fuga fisica. Chamar a cada ~20 s enquanto houver segredos
 * visiveis. */
void hsv3_oled_antiburn_step(void);

/* Desliga o painel e corta o Vext. Usar no fim da cerimonia. */
void hsv3_oled_off(void);

#ifdef __cplusplus
}
#endif

#endif /* HSV3_OLED_H */
