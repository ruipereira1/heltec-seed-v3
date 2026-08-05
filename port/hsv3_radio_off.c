/* Manter o radio calado.
 *
 * O Heltec V3 traz um SX1262 (LoRa) ligado por SPI. Numa maquina que gera
 * chaves privadas, um transmissor e a ultima coisa que se quer viva. Alem de
 * nunca inicializar o SPI nem a stack LoRa, este ficheiro poe o chip
 * fisicamente em reset e deixa-o la.
 *
 * O Wi-Fi e o Bluetooth nem sequer entram no binario: ficam desligados no
 * sdkconfig.defaults, e tools/check_no_radio.py falha o build se algum simbolo
 * de radio aparecer no ELF final. Aqui trata-se so do que e' externo ao SoC.
 */

#include "driver/gpio.h"
#include "hsv3_board.h"

void hsv3_radio_off(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << HSV3_PIN_LORA_RST) |
                        (1ULL << HSV3_PIN_LORA_CS)  |
                        (1ULL << HSV3_PIN_LORA_SCK) |
                        (1ULL << HSV3_PIN_LORA_MOSI),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    /* RST em baixo = SX1262 em reset permanente.
     * CS em alto = nunca selecionado, mesmo que algo mexesse no barramento. */
    gpio_set_level(HSV3_PIN_LORA_RST, 0);
    gpio_set_level(HSV3_PIN_LORA_CS, 1);
    gpio_set_level(HSV3_PIN_LORA_SCK, 0);
    gpio_set_level(HSV3_PIN_LORA_MOSI, 0);

    /* MISO, BUSY e DIO1 sao saidas do radio (dados e IRQ): ficam como entradas
     * sem pull, para nao lhes injetar corrente enquanto o chip esta em reset.
     * DIO1 ficava de fora deste grupo por inconsistencia -- o pino estava
     * declarado em hsv3_board.h mas nunca usado em lado nenhum. */
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << HSV3_PIN_LORA_MISO) | (1ULL << HSV3_PIN_LORA_BUSY) |
                        (1ULL << HSV3_PIN_LORA_DIO1),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);
}
