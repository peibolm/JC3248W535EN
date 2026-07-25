#include "usb_msc.h"

#include "esp_log.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"

#include "storage_sd.h"

static const char *TAG = "usb_msc";

esp_err_t usb_msc_start(void)
{
    /* Suelta la SD del uso normal de la app; el handle que usaba
     * esp_vfs_fat_sdspi_mount() no es reutilizable tras esto. */
    storage_sd_unmount();

    sdmmc_card_t *card = NULL;
    esp_err_t ret = storage_sd_init_raw_for_msc(&card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar la SD para modo USB: %s", esp_err_to_name(ret));
        return ret;
    }

    const tinyusb_msc_sdmmc_config_t config_sdmmc = {
        .card = card,
    };
    ret = tinyusb_msc_storage_init_sdmmc(&config_sdmmc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar almacenamiento MSC: %s", esp_err_to_name(ret));
        return ret;
    }

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,        /* descriptores por defecto de esp_tinyusb */
        .string_descriptor = NULL,
        .string_descriptor_count = 0,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };
    ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al instalar el driver USB: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Modo USB Mass Storage activo");
    return ESP_OK;
}
