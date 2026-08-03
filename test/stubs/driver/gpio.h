/* Substituto de driver/gpio.h. Ver ../README.md. */
#ifndef STUB_DRIVER_GPIO_H
#define STUB_DRIVER_GPIO_H
#include <stdint.h>

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
static inline int gpio_get_level(int pin) { (void)pin; return 1; }
#endif
