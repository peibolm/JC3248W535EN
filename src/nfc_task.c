#include "nfc_task.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "pn532.h"
#include "app_events.h"

static const char *TAG = "nfc_task";

#define NFC_SDA GPIO_NUM_17
#define NFC_SCL GPIO_NUM_18
#define NFC_I2C_PORT I2C_NUM_1

#define NFC_POLL_PERIOD_MS  300
#define NFC_READ_TIMEOUT_MS 500

static void uid_to_hex(const uint8_t *uid, uint8_t len, char *out, size_t out_size)
{
    size_t pos = 0;
    for (uint8_t i = 0; i < len && pos + 2 < out_size; i++) {
        int written = snprintf(&out[pos], out_size - pos, "%02X", uid[i]);
        if (written < 0) {
            break;
        }
        pos += (size_t)written;
    }
    out[pos] = '\0';
}

static void nfc_poll_task(void *arg)
{
    char last_uid_hex[APP_EVENT_UID_HEX_MAX_LEN] = {0};
    bool tag_present = false;

    while (1) {
        uint8_t uid[PN532_MAX_UID_LEN];
        uint8_t uid_len = 0;
        esp_err_t ret = pn532_read_passive_target_uid(uid, &uid_len, NFC_READ_TIMEOUT_MS);

        if (ret == ESP_OK) {
            char uid_hex[APP_EVENT_UID_HEX_MAX_LEN];
            uid_to_hex(uid, uid_len, uid_hex, sizeof(uid_hex));

            /* Solo se notifica en el flanco de "tag nuevo acercado", para no
             * reenviar el mismo evento en cada ciclo de polling mientras la
             * caja sigue apoyada en el lector. */
            if (!tag_present || strcmp(uid_hex, last_uid_hex) != 0) {
                ESP_LOGI(TAG, "Tag detectado: %s", uid_hex);
                app_event_t evt = {.type = APP_EVT_NFC_TAG};
                strlcpy(evt.data.nfc_tag.uid_hex, uid_hex, sizeof(evt.data.nfc_tag.uid_hex));
                app_events_post(&evt);
                strlcpy(last_uid_hex, uid_hex, sizeof(last_uid_hex));
            }
            tag_present = true;
        } else if (ret == ESP_ERR_NOT_FOUND) {
            tag_present = false;
        } else {
            ESP_LOGW(TAG, "Error leyendo PN532: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(NFC_POLL_PERIOD_MS));
    }
}

esp_err_t nfc_task_start(void)
{
    esp_err_t ret = pn532_init(NFC_I2C_PORT, NFC_SDA, NFC_SCL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar I2C del PN532: %s", esp_err_to_name(ret));
        return ret;
    }

    uint32_t version = 0;
    ret = pn532_get_firmware_version(&version);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se detecta PN532: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "PN532 detectado, firmware 0x%08" PRIx32, version);

    ret = pn532_sam_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en SAMConfiguration: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = pn532_set_passive_activation_retries(0x01);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al configurar reintentos RF: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(nfc_poll_task, "nfc_poll", 4096, NULL, 4, NULL);
    return (task_ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
