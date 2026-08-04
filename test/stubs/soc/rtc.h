/* Substituto de soc/rtc.h. Ver ../README.md.
 *
 * rtc_time_get() devolve o contador do RTC_SLOW_CLK -- o RC interno, o
 * oscilador que a fonte de jitter mede contra o cristal.
 *
 * Avanca SEMPRE, mesmo com cycles_frozen ligado. Se parasse, o laco de espera
 * activa do jitter_sample() nunca terminava e o teste ficava pendurado em vez
 * de falhar. O caso "fonte presa" simula-se congelando a contagem de ciclos,
 * que e o outro lado da medicao.
 */
#ifndef STUB_SOC_RTC_H
#define STUB_SOC_RTC_H
#include <stdint.h>
#include "hsv3_stub_ctl.h"

static inline uint64_t rtc_time_get(void)
{
    return ++hsv3_stub.rtc_ticks;
}
#endif
