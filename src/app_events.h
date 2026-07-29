/*
 * Tipos de evento y cola compartida entre las tareas de NFC, báscula, UI
 * (callbacks de botón LVGL) y la máquina de estados de la app (app_fsm),
 * que es la única consumidora.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_EVENT_UID_HEX_MAX_LEN 16
#define APP_EVENT_KEYPAD_TEXT_MAX_LEN 16

typedef enum {
    APP_EVT_NFC_TAG,
    APP_EVT_SCALE_STABLE,
    APP_EVT_SCALE_READING, /* lectura "en vivo", una por cada muestra valida (estable o no) */
    APP_EVT_SCALE_TIMEOUT,
    APP_EVT_UI_CONFIRM_PRESSED,
    APP_EVT_UI_CANCEL_PRESSED,
    APP_EVT_UI_RETRY_PRESSED,
    APP_EVT_UI_KEYPAD_CONFIRMED,
    APP_EVT_UI_USB_MODE_PRESSED,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    union {
        struct {
            char uid_hex[APP_EVENT_UID_HEX_MAX_LEN]; /* hex mayúsculas, sin separadores */
        } nfc_tag;
        struct {
            float weight_g;
        } scale_stable;
        struct {
            float weight_g;
            bool stable; /* true si la ventana de muestras actual ya cumple la tolerancia */
        } scale_reading;
        struct {
            char text[APP_EVENT_KEYPAD_TEXT_MAX_LEN]; /* lo tecleado (digitos y opcionalmente un '.') */
        } keypad;
    } data;
} app_event_t;

extern QueueHandle_t g_app_event_queue;

/**
 * @brief Crea la cola de eventos de la app.
 *
 * Debe llamarse una única vez, antes de arrancar cualquier tarea que envíe
 * o consuma eventos (nfc_task, scale_task, app_fsm).
 */
void app_events_init(void);

/** Envío no bloqueante; descarta el evento (con log) si la cola está llena. */
void app_events_post(const app_event_t *event);

#ifdef __cplusplus
}
#endif
