/* Substituto de esp_cpu.h. Ver README.md nesta pasta. */
#ifndef STUB_ESP_CPU_H
#define STUB_ESP_CPU_H
#include <stdint.h>
#include "hsv3_stub_ctl.h"

/* Avanca de forma irregular, como uma contagem real medida entre dois
 * osciladores. Com cycles_frozen o valor nunca muda -- e' o caso que o
 * firmware tem de apanhar. */
static inline uint32_t esp_cpu_get_cycle_count(void)
{
    if (hsv3_stub.cycles_frozen) return 12345u;
    hsv3_stub.cycles += 7919u + (hsv3_stub.cycles % 13u);
    return hsv3_stub.cycles;
}
#endif
