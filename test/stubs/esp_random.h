/* Substituto de esp_random.h. Ver README.md nesta pasta. */
#ifndef STUB_ESP_RANDOM_H
#define STUB_ESP_RANDOM_H
#include <stddef.h>
#include <stdint.h>
#include "hsv3_stub_ctl.h"

static inline uint32_t esp_random(void)
{
    if (hsv3_stub.rng_stuck) return 0x5A5A5A5Au;
    hsv3_stub.rng_state = hsv3_stub.rng_state * 1664525u + 1013904223u;
    return hsv3_stub.rng_state;
}

static inline void esp_fill_random(void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) p[i] = (uint8_t)(esp_random() >> 24);
}
#endif
