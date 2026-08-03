/* Pinos do Heltec WiFi LoRa 32 V3 (ESP32-S3FN8).
 *
 * CONFIRMA ESTES NUMEROS CONTRA A SERIGRAFIA DA TUA PLACA antes de soldar seja
 * o que for. As fontes publicas divergem entre revisoes (ha paginas que dao o
 * OLED em 41/42, que e' de outra variante). Os valores abaixo sao os da V3.
 */
#ifndef HSV3_BOARD_H
#define HSV3_BOARD_H

/* OLED SSD1306 128x64, por I2C */
#define HSV3_PIN_OLED_SDA   17
#define HSV3_PIN_OLED_SCL   18
#define HSV3_PIN_OLED_RST   21
#define HSV3_PIN_VEXT       36   /* alimenta o OLED; activo-BAIXO */

#define HSV3_PIN_LED        35

/* SX1262. Nunca inicializado -- ver hsv3_radio_off.c */
#define HSV3_PIN_LORA_CS     8
#define HSV3_PIN_LORA_SCK    9
#define HSV3_PIN_LORA_MOSI  10
#define HSV3_PIN_LORA_MISO  11
#define HSV3_PIN_LORA_RST   12
#define HSV3_PIN_LORA_BUSY  13
#define HSV3_PIN_LORA_DIO1  14

/* Botao. Um so, e e' o que a placa ja tem.
 *
 * A placa tem dois botoes fisicos, mas o RST esta ligado ao pino EN/CHIP_PU:
 * faz reset de hardware, o firmware nunca o ve, e um reset por EN tambem limpa
 * a RTC memory. So o PRG e legivel por software.
 *
 * Houve aqui uma deteccao de dois botoes opcionais em GPIO5/GPIO6, para quem os
 * quisesse soldar. Foi removida: obrigava a um ecra de deteccao, a um modo
 * alternativo em todos os menus e a tres valores de evento que a UI de um botao
 * nunca produz -- tudo para uma soldadura que ninguem faz. Menos codigo e menos
 * caminhos e' o que torna este firmware auditavel. */
#define HSV3_PIN_BTN_OK      0   /* PRG, ja na placa. Strapping: nao segurar no arranque */

#define HSV3_OLED_I2C_ADDR  0x3C
#define HSV3_OLED_WIDTH     128
#define HSV3_OLED_HEIGHT    64

#endif /* HSV3_BOARD_H */
