/* Substituto de freertos/task.h: nao ha escalonador, so avanca o relogio. */
#ifndef STUB_FREERTOS_TASK_H
#define STUB_FREERTOS_TASK_H
#include "hsv3_stub_ctl.h"
static inline void vTaskDelay(int ticks) { hsv3_stub.now_us += (int64_t)ticks * 1000; }
#endif
