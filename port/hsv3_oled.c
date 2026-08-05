#include "hsv3_oled.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hsv3_board.h"
#include "hsv3_font.h"

#define FB_SIZE (HSV3_OLED_WIDTH * HSV3_OLED_HEIGHT / 8)   /* 1024 bytes */

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static uint8_t s_fb[FB_SIZE];
static int s_shift_x, s_shift_y;
static int s_ready;

static void cmd(uint8_t c)
{
    const uint8_t buf[2] = { 0x00, c };   /* 0x00 = byte de comando */
    if (s_ready) i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

int hsv3_oled_init(void)
{
    /* Vext alimenta o OLED e e' activo-BAIXO. */
    gpio_config_t vext = {
        .pin_bit_mask = (1ULL << HSV3_PIN_VEXT) | (1ULL << HSV3_PIN_OLED_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&vext);
    gpio_set_level(HSV3_PIN_VEXT, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* pulso de reset do painel */
    gpio_set_level(HSV3_PIN_OLED_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(HSV3_PIN_OLED_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = HSV3_PIN_OLED_SDA,
        .scl_io_num = HSV3_PIN_OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) return -1;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HSV3_OLED_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) return -1;
    s_ready = 1;

    static const uint8_t init_seq[] = {
        0xAE,             /* display off */
        0xD5, 0x80,       /* clock */
        0xA8, 0x3F,       /* multiplex = 64 linhas */
        0xD3, 0x00,       /* sem offset */
        0x40,             /* start line 0 */
        0x8D, 0x14,       /* charge pump ligado */
        0x20, 0x00,       /* enderecamento horizontal */
        0xA1,             /* segment remap: o Heltec monta o painel ao contrario */
        0xC8,             /* varrimento COM invertido, pela mesma razao */
        0xDA, 0x12,       /* configuracao dos pinos COM */
        0x81, 0x7F,       /* contraste */
        0xD9, 0xF1,       /* pre-charge */
        0xDB, 0x40,       /* VCOM deselect */
        0xA4,             /* mostra a RAM, nao tudo aceso */
        0xA6,             /* nao invertido */
        0xAF,             /* display on */
    };
    for (size_t i = 0; i < sizeof(init_seq); i++) cmd(init_seq[i]);

    hsv3_oled_clear();
    hsv3_oled_flush();
    return 0;
}

void hsv3_oled_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

static void put_char(int px, int row, char ch, int invert)
{
    if (row < 0 || row >= HSV3_OLED_ROWS) return;
    if (ch < 32 || ch > 126) ch = '?';

    for (int c = 0; c < HSV3_FONT_ADVANCE; c++) {
        int x = px + c;
        if (x < 0 || x >= HSV3_OLED_WIDTH) continue;
        uint8_t bits = (c < HSV3_FONT_WIDTH)
            ? hsv3_font5x7[ch - 32][c]
            : 0x00;                       /* coluna de espaco entre caracteres */
        /* A fonte tem o bit 0 em cima; a pagina do SSD1306 tambem. Desloca-se
         * 1 pixel para baixo para o texto nao ficar colado ao topo. */
        s_fb[row * HSV3_OLED_WIDTH + x] = invert ? (uint8_t)~(bits << 1)
                                                 : (uint8_t)(bits << 1);
    }
}

static void draw(int col, int row, const char *s, int invert)
{
    int px = col * HSV3_FONT_ADVANCE;
    for (; *s && px < HSV3_OLED_WIDTH; s++, px += HSV3_FONT_ADVANCE) {
        put_char(px, row, *s, invert);
    }
}

void hsv3_oled_text(int col, int row, const char *s)     { draw(col, row, s, 0); }
void hsv3_oled_text_inv(int col, int row, const char *s) { draw(col, row, s, 1); }

void hsv3_oled_flush(void)
{
    if (!s_ready) return;

    cmd(0x21); cmd(0x00); cmd(HSV3_OLED_WIDTH - 1);   /* colunas 0..127 */
    cmd(0x22); cmd(0x00); cmd(HSV3_OLED_ROWS - 1);    /* paginas 0..7 */

    /* Envia pagina a pagina, aplicando o deslocamento anti burn-in. */
    for (int page = 0; page < HSV3_OLED_ROWS; page++) {
        uint8_t buf[1 + HSV3_OLED_WIDTH];
        int src_page = (page - s_shift_y + HSV3_OLED_ROWS) % HSV3_OLED_ROWS;
        buf[0] = 0x40;   /* 0x40 = bytes de dados */
        for (int x = 0; x < HSV3_OLED_WIDTH; x++) {
            int src_x = (x - s_shift_x + HSV3_OLED_WIDTH) % HSV3_OLED_WIDTH;
            buf[1 + x] = s_fb[src_page * HSV3_OLED_WIDTH + src_x];
        }
        i2c_master_transmit(s_dev, buf, sizeof(buf), 200);
    }
}

void hsv3_oled_page(const char *lines[])
{
    hsv3_oled_clear();
    for (int r = 0; r < HSV3_OLED_ROWS && lines[r] != NULL; r++) {
        hsv3_oled_text(0, r, lines[r]);
    }
    hsv3_oled_flush();
}

void hsv3_oled_antiburn_step(void)
{
    /* Percorre um quadrado de 3x2 pixeis. Imperceptivel a ler, mas chega para
     * a imagem nunca ficar parada no mesmo sitio o suficiente para marcar. */
    s_shift_x = (s_shift_x + 1) % 3;
    if (s_shift_x == 0) s_shift_y = (s_shift_y + 1) % 2;
    hsv3_oled_flush();
}

void hsv3_oled_off(void)
{
    hsv3_oled_clear();
    hsv3_oled_flush();
    cmd(0xAE);                              /* display off */
    s_ready = 0;
    gpio_set_level(HSV3_PIN_VEXT, 1);       /* corta a alimentacao do painel */
}

static void set_pixel(int x, int y, int on)
{
    if (x < 0 || x >= HSV3_OLED_WIDTH || y < 0 || y >= HSV3_OLED_HEIGHT) return;
    uint8_t mask = (uint8_t)(1u << (y & 7));
    uint8_t *p = &s_fb[(y >> 3) * HSV3_OLED_WIDTH + x];
    if (on) *p |= mask; else *p = (uint8_t)(*p & ~mask);
}

void hsv3_oled_bigchar(int x, int y, char ch, int scale)
{
    if (ch < 32 || ch > 126) ch = '?';
    if (scale < 1) scale = 1;
    for (int c = 0; c < HSV3_FONT_WIDTH; c++) {
        uint8_t bits = hsv3_font5x7[ch - 32][c];
        for (int r = 0; r < HSV3_FONT_HEIGHT; r++) {
            if (!((bits >> r) & 1)) continue;
            for (int dx = 0; dx < scale; dx++) {
                for (int dy = 0; dy < scale; dy++) {
                    set_pixel(x + c * scale + dx, y + r * scale + dy, 1);
                }
            }
        }
    }
}

void hsv3_oled_hline(int x0, int x1, int y, int on)
{
    for (int x = x0; x <= x1; x++) set_pixel(x, y, on);
}

void hsv3_oled_bar(int x, int y, int w, int h, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    /* moldura */
    for (int i = 0; i < w; i++) { set_pixel(x + i, y, 1); set_pixel(x + i, y + h - 1, 1); }
    for (int j = 0; j < h; j++) { set_pixel(x, y + j, 1); set_pixel(x + w - 1, y + j, 1); }
    /* enchimento */
    int fill = (w - 4) * pct / 100;
    for (int i = 0; i < fill; i++) {
        for (int j = 2; j < h - 2; j++) set_pixel(x + 2 + i, y + j, 1);
    }
}
