/* Substituto de driver/i2c_master.h: conta transmissoes e deita fora os bytes.
 * O que se testa e' o framebuffer antes de sair, nao a saida. */
#ifndef STUB_DRIVER_I2C_MASTER_H
#define STUB_DRIVER_I2C_MASTER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hsv3_stub_ctl.h"

#define ESP_OK 0
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0

typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;

typedef struct {
    int i2c_port, sda_io_num, scl_io_num, clk_source, glitch_ignore_cnt;
    struct { int enable_internal_pullup; } flags;
} i2c_master_bus_config_t;

typedef struct {
    int dev_addr_length, device_address, scl_speed_hz;
} i2c_device_config_t;

static inline int i2c_new_master_bus(const i2c_master_bus_config_t *c,
                                     i2c_master_bus_handle_t *out)
{ (void)c; *out = (void *)1; return ESP_OK; }

static inline int i2c_master_bus_add_device(i2c_master_bus_handle_t b,
                                            const i2c_device_config_t *c,
                                            i2c_master_dev_handle_t *out)
{ (void)b; (void)c; *out = (void *)1; return ESP_OK; }

static inline int i2c_master_transmit(i2c_master_dev_handle_t d,
                                      const uint8_t *buf, size_t len, int to)
{ (void)d; (void)buf; (void)len; (void)to; hsv3_stub.i2c_writes++; return ESP_OK; }
#endif
