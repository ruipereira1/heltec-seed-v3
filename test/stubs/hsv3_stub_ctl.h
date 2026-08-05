/* Controlo dos substitutos do ESP-IDF. Ver test/stubs/README.md. */
#ifndef HSV3_STUB_CTL_H
#define HSV3_STUB_CTL_H

#include <stdint.h>

/* Estado partilhado por todos os substitutos. O teste mexe nisto para forcar
 * os casos que na placa nao se conseguem provocar a pedido -- uma fonte presa,
 * um relogio que nao anda -- e confirmar que o firmware recusa em vez de
 * continuar com entropia degradada. */
#define HSV3_STUB_BTN_MAX 32

typedef struct {
    int      rng_stuck;      /* 1 = esp_random() devolve sempre o mesmo valor */
    uint32_t rng_state;
    int      cycles_frozen;  /* 1 = a contagem de ciclos nunca varia */
    uint32_t cycles;
    int64_t  now_us;
    uint64_t rtc_ticks;      /* contador do RC lento */
    int      i2c_writes;     /* quantas transmissoes o OLED tentou */

    /* Agenda do botao PRG: instantes (em now_us) em que o nivel troca.
     * btn_level0 e' o nivel antes do primeiro instante agendado (1 = solto,
     * activo-baixo). gpio_get_level() percorre isto sozinho -- ver
     * test/stubs/driver/gpio.h. Existe para poder testar hsv3_buttons_select()
     * no PC com duracoes de premir exactas, sem um dedo a serio. */
    int64_t  btn_flip_at[HSV3_STUB_BTN_MAX];
    int      btn_n_flips;
    int      btn_level0;
} hsv3_stub_t;

extern hsv3_stub_t hsv3_stub;

void hsv3_stub_reset(void);

#endif /* HSV3_STUB_CTL_H */
