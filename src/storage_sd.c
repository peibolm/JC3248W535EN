#include "storage_sd.h"

#include <stdlib.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"

#include "bsp_err_check.h"

static const char *TAG = "storage_sd";

#define SD_PIN_CLK  GPIO_NUM_12
#define SD_PIN_MOSI GPIO_NUM_11
#define SD_PIN_MISO GPIO_NUM_13
#define SD_PIN_CS   GPIO_NUM_10
#define SD_SPI_HOST SPI3_HOST

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

esp_err_t storage_sd_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SPI3 bus for microSD");
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    BSP_ERROR_CHECK_RETURN_ERR(spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = SD_SPI_HOST;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(STORAGE_SD_BASE_PATH, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem on microSD card");
        } else {
            ESP_LOGE(TAG, "Failed to initialize microSD card (%s)", esp_err_to_name(ret));
        }
        spi_bus_free(SD_SPI_HOST);
        return ret;
    }

    sdmmc_card_print_info(stdout, s_card);
    s_mounted = true;
    return ESP_OK;
}

esp_err_t storage_sd_unmount(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(STORAGE_SD_BASE_PATH, s_card);
    spi_bus_free(SD_SPI_HOST);
    s_card = NULL;
    s_mounted = false;
    return ret;
}

bool storage_sd_is_mounted(void)
{
    return s_mounted;
}

esp_err_t storage_sd_init_raw_for_msc(sdmmc_card_t **out_card)
{
    ESP_LOGI(TAG, "Initializing SPI3 bus for microSD (modo USB, sin FATFS)");
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    BSP_ERROR_CHECK_RETURN_ERR(spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    BSP_ERROR_CHECK_RETURN_ERR(host.init());

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = SD_SPI_HOST;

    sdmmc_card_t *card = malloc(sizeof(sdmmc_card_t));
    if (!card) {
        spi_bus_free(SD_SPI_HOST);
        return ESP_ERR_NO_MEM;
    }

    sdspi_dev_handle_t device_handle = 0;
    esp_err_t ret = sdspi_host_init_device(&slot_config, &device_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdspi_host_init_device failed (%s)", esp_err_to_name(ret));
        free(card);
        spi_bus_free(SD_SPI_HOST);
        return ret;
    }
    host.slot = device_handle;

    ret = sdmmc_card_init(&host, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init failed (%s)", esp_err_to_name(ret));
        free(card);
        spi_bus_free(SD_SPI_HOST);
        return ret;
    }

    *out_card = card;
    return ESP_OK;
}
