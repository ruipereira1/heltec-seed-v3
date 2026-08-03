#include "hsv3_buttons.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "hsv3_board.h"
#include "hsv3_rng.h"

/* Limiares contiguos, de proposito: nao pode haver nenhuma duracao de toque
 * que o firmware descarte em silencio.
 *
 * A primeira versao tinha SHORT<=400 e LONG>=600, o que criava uma zona morta
 * entre os dois. Como o cronometro so arranca depois do debounce, um toque de
 * 600 ms reais media ~575 ms e caia no buraco -- o toque nao fazia nada e nao
 * havia forma de o utilizador perceber porque. */
/* Nunca zero ticks: com vTaskDelay(0) o laco nao cede o CPU. */
#define POLL_TICKS   (pdMS_TO_TICKS(5) > 0 ? pdMS_TO_TICKS(5) : 1)
#define DEBOUNCE_MS    25
#define SHORT_MAX_MS  350    /* < 350 ms   -> SHORT (muda de opcao)  */
                             /* >= 350 ms  -> LONG  (escolhe)        */
#define MAX_HOLD_MS  4000    /* nao esperar para sempre por uma largada */

static const char *TAG = "btn";

static int s_last_ms = 0;

static int pressed(int pin)
{
    /* O botao liga o pino a GND, com pull-up interno: carregado == 0. */
    return gpio_get_level(pin) == 0;
}

void hsv3_buttons_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << HSV3_PIN_BTN_OK),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

hsv3_btn_t hsv3_buttons_wait(int timeout_ms)
{
    int64_t deadline = (timeout_ms < 0)
        ? INT64_MAX
        : esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    for (;;) {
        if (esp_timer_get_time() >= deadline) return HSV3_BTN_NONE;

        if (!pressed(HSV3_PIN_BTN_OK)) {
            vTaskDelay(POLL_TICKS);
            continue;
        }

        /* Cada flanco alimenta o acumulador de timing. Os bits baixos da
         * contagem de ciclos no instante do toque humano sao imprevisiveis. */
        hsv3_rng_timing_feed(esp_cpu_get_cycle_count());

        /* O cronometro arranca ANTES do debounce. Se arrancasse depois, todos
         * os toques mediam 25 ms a menos do que duraram de facto. */
        int64_t t0 = esp_timer_get_time();
        ESP_LOGI(TAG, "premido");
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));

        /* A duracao decide o evento. */
        while (pressed(HSV3_PIN_BTN_OK)) {
            if ((esp_timer_get_time() - t0) / 1000 >= MAX_HOLD_MS) break;
            vTaskDelay(POLL_TICKS);
        }
        int64_t held_ms = (esp_timer_get_time() - t0) / 1000;
        s_last_ms = (int)held_ms;
        hsv3_rng_timing_feed(esp_cpu_get_cycle_count());

        /* Se chegou ao limiar de HOLD, espera que largue para nao repetir. */
        while (pressed(HSV3_PIN_BTN_OK)) vTaskDelay(pdMS_TO_TICKS(10));

        /* Dois gestos, sem janela intermedia para acertar: qualquer pressao a
         * partir de 350 ms escolhe, dure meio segundo ou cinco. A versao
         * anterior tinha um terceiro gesto entre 350 e 1500 ms e ninguem
         * conseguia la cair -- os toques reais medem ~170 ms ou passam de
         * 1500 ms, nunca o meio. */
        hsv3_btn_t ev = (held_ms >= SHORT_MAX_MS) ? HSV3_BTN_LONG : HSV3_BTN_SHORT;
        ESP_LOGI(TAG, "largado apos %lldms -> %s", held_ms,
                 ev == HSV3_BTN_LONG ? "LONG(escolhe)" : "SHORT(muda)");
        return ev;
    }
}

int hsv3_buttons_last_ms(void)
{
    return s_last_ms;
}

#define SCROLL_STEP_MS 400   /* velocidade a que as opcoes passam ao segurar */

int hsv3_buttons_select(int n_options, int start, void (*redraw)(int idx))
{
    int cur = (n_options > 0) ? (start % n_options) : 0;

    if (n_options <= 0) return 0;
    if (redraw) redraw(cur);

    for (;;) {
        /* espera que carreguem */
        while (!pressed(HSV3_PIN_BTN_OK)) vTaskDelay(POLL_TICKS);

        hsv3_rng_timing_feed(esp_cpu_get_cycle_count());
        int64_t t0 = esp_timer_get_time();
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));

        int scrolled = 0;
        int64_t next_step = t0 + (int64_t)SHORT_MAX_MS * 1000;

        while (pressed(HSV3_PIN_BTN_OK)) {
            int64_t now = esp_timer_get_time();
            if (now >= next_step) {
                /* passou o limiar: entra em modo de rolagem */
                cur = (cur + 1) % n_options;
                scrolled = 1;
                if (redraw) redraw(cur);
                next_step = now + (int64_t)SCROLL_STEP_MS * 1000;
            }
            vTaskDelay(POLL_TICKS);
        }

        int64_t held_ms = (esp_timer_get_time() - t0) / 1000;
        s_last_ms = (int)held_ms;
        hsv3_rng_timing_feed(esp_cpu_get_cycle_count());

        if (scrolled) {
            /* largou durante a rolagem -> escolhe o que estava a mostrar */
            ESP_LOGI(TAG, "rolagem %lldms -> escolhe %d", held_ms, cur);
            return cur;
        }
        /* toque rapido -> so avanca uma posicao, nao escolhe */
        cur = (cur + 1) % n_options;
        if (redraw) redraw(cur);
        ESP_LOGI(TAG, "toque %lldms -> avanca para %d", held_ms, cur);
    }
}

int hsv3_buttons_count_taps(int pause_ms, void (*redraw)(int taps, int pct))
{
    int taps = 0;

    /* espera pelo primeiro toque, sem limite de tempo */
    while (!pressed(HSV3_PIN_BTN_OK)) vTaskDelay(POLL_TICKS);

    for (;;) {
        /* conta este toque e espera que largue */
        hsv3_rng_timing_feed(esp_cpu_get_cycle_count());
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        while (pressed(HSV3_PIN_BTN_OK)) vTaskDelay(POLL_TICKS);
        hsv3_rng_timing_feed(esp_cpu_get_cycle_count());
        taps++;
        if (redraw) redraw(taps, 0);

        /* janela de pausa: se ninguem tocar, o valor fica gravado */
        int64_t t0 = esp_timer_get_time();
        int last_pct = -1;
        for (;;) {
            if (pressed(HSV3_PIN_BTN_OK)) break;          /* mais um toque */
            int64_t elapsed = (esp_timer_get_time() - t0) / 1000;
            if (elapsed >= pause_ms) {
                ESP_LOGI(TAG, "%d toques -> grava", taps);
                return taps;
            }
            int pct = (int)(elapsed * 100 / pause_ms);
            if (redraw && pct / 10 != last_pct / 10) {     /* so a cada 10% */
                redraw(taps, pct);
                last_pct = pct;
            }
            vTaskDelay(POLL_TICKS);
        }
    }
}
