/* Controlo dos substitutos do ESP-IDF. Ver test/stubs/README.md. */
#ifndef HSV3_STUB_CTL_H
#define HSV3_STUB_CTL_H

#include <stdint.h>

/* Estado partilhado por todos os substitutos. O teste mexe nisto para forcar
 * os casos que na placa nao se conseguem provocar a pedido -- uma fonte presa,
 * um relogio que nao anda -- e confirmar que o firmware recusa em vez de
 * continuar com entropia degradada. */
typedef struct {
    int      rng_stuck;      /* 1 = esp_random() devolve sempre o mesmo valor */
    uint32_t rng_state;
    int      cycles_frozen;  /* 1 = a contagem de ciclos nunca varia */
    uint32_t cycles;
    int64_t  now_us;
    uint64_t rtc_ticks;      /* contador do RC lento */
    int      i2c_writes;     /* quantas transmissoes o OLED tentou */
} hsv3_stub_t;

extern hsv3_stub_t hsv3_stub;

void hsv3_stub_reset(void);

#endif /* HSV3_STUB_CTL_H */
