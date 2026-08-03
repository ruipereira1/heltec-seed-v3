/* Substituto de esp_log.h: os testes nao querem ruido na saida. */
#ifndef STUB_ESP_LOG_H
#define STUB_ESP_LOG_H
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOGV(tag, ...) ((void)(tag))
#endif
