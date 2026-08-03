/* Substituto de esp_timer.h. Ver README.md nesta pasta. */
#ifndef STUB_ESP_TIMER_H
#define STUB_ESP_TIMER_H
#include <stdint.h>
#include "hsv3_stub_ctl.h"

/* Cada leitura avanca 10 us: os laços de espera activa terminam sempre, e os
 * testes correm em milissegundos em vez de segundos. */
static inline int64_t esp_timer_get_time(void)
{
    hsv3_stub.now_us += 10;
    return hsv3_stub.now_us;
}
#endif
