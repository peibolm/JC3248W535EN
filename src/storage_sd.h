/*
 * Montaje de la tarjeta microSD (FAT sobre SPI3_HOST) para almacenar
 * datos_maestros.csv e inventario.csv.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_SD_BASE_PATH "/sdcard"

/**
 * @brief Monta la tarjeta microSD en STORAGE_SD_BASE_PATH.
 *
 * Usa SPI3_HOST en exclusiva (la pantalla usa SPI2_HOST con pines distintos,
 * no es posible ni necesario compartir bus).
 *
 * @return ESP_OK si se monta correctamente.
 */
esp_err_t storage_sd_mount(void);

/**
 * @brief Desmonta la tarjeta microSD y libera el bus SPI3.
 */
esp_err_t storage_sd_unmount(void);

/**
 * @brief Indica si la tarjeta está montada actualmente.
 */
bool storage_sd_is_mounted(void);

/**
 * @brief Inicializa la SD "en crudo" (sin montar FATFS), para entregarla al
 * modo USB Mass Storage. Usar SOLO tras storage_sd_unmount(): el modo USB
 * necesita su propio handle de tarjeta, independiente del que usa
 * esp_vfs_fat_sdspi_mount() internamente (ese se libera por completo al
 * desmontar y no se puede reutilizar).
 *
 * @param out_card Handle de la tarjeta lista para tinyusb_msc_storage_init_sdmmc().
 */
esp_err_t storage_sd_init_raw_for_msc(sdmmc_card_t **out_card);

#ifdef __cplusplus
}
#endif
