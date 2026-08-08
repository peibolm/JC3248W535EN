#include "usb_msc.h"

#include "esp_log.h"
#include "esp_system.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "storage_sd.h"

static const char *TAG = "usb_msc";

#define NVS_NAMESPACE "usb_msc"
#define NVS_KEY_BOOT  "boot_usb"

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

/* NVS puede fallar la primera vez con ESP_ERR_NVS_NO_FREE_PAGES o
 * ESP_ERR_NVS_NEW_VERSION_FOUND (particion nueva/de otra version de IDF);
 * el propio ejemplo de ESP-IDF resuelve esto borrando e inicializando de
 * nuevo, es un caso esperado y no un fallo real. */
static esp_err_t nvs_init_once(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

void usb_msc_request_boot_and_restart(void)
{
    if (nvs_init_once() == ESP_OK) {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_u8(handle, NVS_KEY_BOOT, 1);
            nvs_commit(handle);
            nvs_close(handle);
        } else {
            ESP_LOGE(TAG, "No se pudo abrir NVS para marcar el arranque en modo USB");
        }
    } else {
        ESP_LOGE(TAG, "No se pudo inicializar NVS; reiniciando igualmente (puede que no entre en modo USB)");
    }

    ESP_LOGI(TAG, "Reiniciando directo a modo USB...");
    esp_restart();
}

bool usb_msc_should_boot_into_usb_mode(void)
{
    if (nvs_init_once() != ESP_OK) {
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    uint8_t flag = 0;
    esp_err_t ret = nvs_get_u8(handle, NVS_KEY_BOOT, &flag);
    if (ret == ESP_OK && flag) {
        /* Se limpia YA, antes de devolver el control: si algo falla luego
         * al entrar en modo USB, el siguiente reinicio es uno normal y no
         * un bucle. */
        nvs_erase_key(handle, NVS_KEY_BOOT);
        nvs_commit(handle);
    }
    nvs_close(handle);

    return (ret == ESP_OK) && flag;
}
