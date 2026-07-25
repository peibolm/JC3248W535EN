/*
 * Tarea de polling del lector NFC (PN532). Nunca toca LVGL: solo envía
 * eventos APP_EVT_NFC_TAG a la cola de la app.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el PN532 (I2C1, GPIO17/GPIO18) y arranca la tarea de
 * polling. Requiere que app_events_init() se haya llamado antes.
 */
esp_err_t nfc_task_start(void);

#ifdef __cplusplus
}
#endif
