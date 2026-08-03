/* Fonte 5x7. O .c e gerado por tools/gen_font.py. */
#ifndef HSV3_FONT_H
#define HSV3_FONT_H

#include <stdint.h>

#define HSV3_FONT_CHARS  95   /* ASCII 32..126 */
#define HSV3_FONT_WIDTH   5
#define HSV3_FONT_HEIGHT  7
#define HSV3_FONT_ADVANCE 6   /* 5 colunas + 1 de espaco */

extern const uint8_t hsv3_font5x7[HSV3_FONT_CHARS][HSV3_FONT_WIDTH];

#endif /* HSV3_FONT_H */
