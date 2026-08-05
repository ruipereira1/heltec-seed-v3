/* Substituto de driver/gpio.h. Ver ../README.md. */
#ifndef STUB_DRIVER_GPIO_H
#define STUB_DRIVER_GPIO_H
#include <stdint.h>

#include "hsv3_board.h"
#include "hsv3_stub_ctl.h"

typedef enum { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT } gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE, GPIO_PULLUP_ENABLE } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE, GPIO_PULLDOWN_ENABLE } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE } gpio_int_type_t;

typedef struct {
    uint64_t        pin_bit_mask;
    gpio_mode_t     mode;
    gpio_pullup_t   pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

static inline int gpio_config(const gpio_config_t *c) { (void)c; return 0; }
static inline int gpio_set_level(int pin, int level) { (void)pin; (void)level; return 0; }

/* Todos os pinos leem sempre solto (1), excepto o PRG, cujo nivel segue a
 * agenda de hsv3_stub.btn_flip_at -- ver ali o porque. */
static inline int gpio_get_level(int pin)
{
    if (pin != HSV3_PIN_BTN_OK) return 1;

    int lvl = hsv3_stub.btn_level0;
    for (int i = 0; i < hsv3_stub.btn_n_flips; i++) {
        if (hsv3_stub.now_us < hsv3_stub.btn_flip_at[i]) break;
        lvl ^= 1;
    }
    return lvl;
}
#endif
