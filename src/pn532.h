/*
 * Mini-driver del protocolo de host PN532 sobre I2C (driver/i2c.h legacy,
 * igual estilo que bsp_i2c_init() en esp_bsp.c). Usa un bus I2C
 * independiente del táctil (I2C_NUM_1, GPIO17/GPIO18 del conector P3/P4).
 *
 * Cubre solo los comandos necesarios para esta app: versión de firmware
 * (test de humo), configuración SAM y lectura de UID de tarjetas pasivas
 * ISO14443A (Mifare/NTAG).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PN532_MAX_UID_LEN 7

/**
 * @brief Inicializa el bus I2C dedicado al PN532 y lo deja listo para operar.
 */
esp_err_t pn532_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl);

/**
 * @brief Test de humo: pide la versión de firmware al PN532.
 *
 * @param out_version IC<<24 | Ver<<16 | Rev<<8 | Support
 */
esp_err_t pn532_get_firmware_version(uint32_t *out_version);

/**
 * @brief Configura el Secure Access Module en modo normal (sin IRQ).
 * Debe llamarse una vez tras pn532_init(), antes de leer tags.
 */
esp_err_t pn532_sam_config(void);

/**
 * @brief Limita los reintentos internos de activacion pasiva del PN532
 * (RFConfiguration, CfgItem 0x05) para que InListPassiveTarget responda
 * rapido cuando no hay tarjeta, en vez de agotar el timeout del host.
 * Llamar una vez tras pn532_sam_config().
 */
esp_err_t pn532_set_passive_activation_retries(uint8_t retries);

/**
 * @brief Intenta detectar y leer el UID de una tarjeta pasiva ISO14443A (106 kbps).
 *
 * Devuelve ESP_ERR_NOT_FOUND si no hay ninguna tarjeta en el campo (esto es
 * normal en el uso habitual: se llama en bucle desde la tarea de polling).
 *
 * @param uid       Buffer de salida, tamaño mínimo PN532_MAX_UID_LEN.
 * @param uid_len   Longitud real del UID leído.
 * @param timeout_ms Tiempo máximo de espera de la respuesta del PN532.
 */
esp_err_t pn532_read_passive_target_uid(uint8_t *uid, uint8_t *uid_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
