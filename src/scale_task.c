#include "scale_task.h"

#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "scale_uart.h"
#include "app_events.h"
#include "settings.h"

static const char *TAG = "scale_task";

#define SCALE_BAUD_RATE 9600 /* confirmado con la báscula real: 9600 8N1 */

#define SCALE_SAMPLE_PERIOD_MS       200

/* Tolerancia, ventana de estabilidad y timeout son ajustables en vivo desde
 * el menu de Ajustes (ver settings.h) - se leen con settings_get() en cada
 * uso, no son constantes. SCALE_STABLE_WINDOW_SAMPLES_MAX es solo el tope
 * para reservar el array de la ventana en la pila: el tamano de ventana
 * REAL en cada pesada es settings_get(SETTING_SCALE_STABLE_WINDOW_SAMPLES),
 * siempre <= este tope porque asi lo valida settings_set(). */
#define SCALE_STABLE_WINDOW_SAMPLES_MAX 20

static volatile bool s_weighing_active = false;
static volatile float s_min_weight_g = 0.0f;
static volatile bool s_reset_window = false;

void scale_request_weighing(float min_weight_g)
{
    /* La bascula transmite en continuo aunque nadie la lea (p.ej. mientras
     * el operario teclea en el numpad); sin este flush, las primeras
     * muestras de esta pesada podrian ser tramas viejas ya acumuladas. */
    scale_uart_flush();
    s_min_weight_g = min_weight_g;
    s_reset_window = true;
    s_weighing_active = true;
}

void scale_continue_weighing(float min_weight_g)
{
    /* Sin flush ni reinicio de ventana: sigue deslizando las muestras ya
     * acumuladas. Si cada re-armado de la vigilancia en segundo plano
     * reiniciase la ventana desde cero, el indicador de estable pasaria
     * brevemente por "inestable" en cada ciclo aunque el peso real no
     * haya cambiado nada (parpadeo rojo/verde). */
    s_min_weight_g = min_weight_g;
    s_weighing_active = true;
}

void scale_cancel_weighing(void)
{
    s_weighing_active = false;
}

static void scale_poll_task(void *arg)
{
    float window[SCALE_STABLE_WINDOW_SAMPLES_MAX];
    size_t window_count = 0;
    size_t window_size = SCALE_STABLE_WINDOW_SAMPLES_MAX;
    TickType_t weighing_start = 0;

    while (1) {
        if (!s_weighing_active) {
            vTaskDelay(pdMS_TO_TICKS(SCALE_SAMPLE_PERIOD_MS));
            continue;
        }

        if (s_reset_window) {
            s_reset_window = false;
            window_count = 0;
        }

        if (window_count == 0) {
            weighing_start = xTaskGetTickCount();
            /* El tamano de ventana se fija solo al arrancar una pesada
             * nueva, no en mitad de una: si cambiase a media ventana, las
             * muestras ya acumuladas dejarian de tener sentido. */
            int configured = (int)settings_get(SETTING_SCALE_STABLE_WINDOW_SAMPLES);
            if (configured < 1) {
                configured = 1;
            } else if (configured > SCALE_STABLE_WINDOW_SAMPLES_MAX) {
                configured = SCALE_STABLE_WINDOW_SAMPLES_MAX;
            }
            window_size = (size_t)configured;
        }

        uint8_t frame[64];
        size_t n = scale_uart_read_frame(frame, sizeof(frame), SCALE_SAMPLE_PERIOD_MS);
        float weight_g;

        if (n > 0 && scale_protocol_parse(frame, n, &weight_g) &&
            fabsf(weight_g) >= s_min_weight_g) {
            if (window_count < window_size) {
                window[window_count++] = weight_g;
            } else {
                memmove(&window[0], &window[1], (window_size - 1) * sizeof(float));
                window[window_size - 1] = weight_g;
            }

            bool is_stable = false;
            float avg = weight_g;
            if (window_count == window_size) {
                float min_w = window[0], max_w = window[0], sum = 0.0f;
                for (size_t i = 0; i < window_size; i++) {
                    if (window[i] < min_w) min_w = window[i];
                    if (window[i] > max_w) max_w = window[i];
                    sum += window[i];
                }
                is_stable = (max_w - min_w) <= settings_get(SETTING_SCALE_WEIGHT_TOLERANCE_G);
                avg = sum / window_size;
            }

            /* Lectura "en vivo" para dar feedback continuo en pantalla
             * (peso actual + si esta estable o aun asentandose), tanto si
             * la ventana ya esta completa/estable como si no. */
            app_event_t reading_evt = {.type = APP_EVT_SCALE_READING};
            reading_evt.data.scale_reading.weight_g = weight_g;
            reading_evt.data.scale_reading.stable = is_stable;
            app_events_post(&reading_evt);

            if (is_stable) {
                ESP_LOGI(TAG, "Peso estable: %.1f g", avg);

                /* Se refresca el reloj del timeout aqui (no solo cuando la
                 * ventana arranca de cero) porque en modo "continue" la
                 * ventana ya no se reinicia entre re-armados: sin esto, el
                 * timeout de 15s se mediria desde un arranque muy antiguo
                 * durante una vigilancia larga. */
                weighing_start = xTaskGetTickCount();

                /* Importante: dejar nuestro propio estado en "terminado"
                 * ANTES de publicar el evento. app_events_post() puede
                 * ceder la CPU de inmediato a app_fsm (mayor prioridad),
                 * que puede re-armar esta misma pesada (p.ej. vigilancia
                 * en WAIT_WEIGHT_USED) antes de que esta tarea retome la
                 * ejecucion; si el reseteo fuera despues del post,
                 * pisariamos ese nuevo "activo" con un "false" viejo. */
                s_weighing_active = false;

                app_event_t evt = {.type = APP_EVT_SCALE_STABLE};
                evt.data.scale_stable.weight_g = avg;
                app_events_post(&evt);
                continue;
            }
        }

        uint32_t timeout_ms = (uint32_t)(settings_get(SETTING_SCALE_STABILIZE_TIMEOUT_S) * 1000.0f);
        if (s_weighing_active &&
            (xTaskGetTickCount() - weighing_start) > pdMS_TO_TICKS(timeout_ms)) {
            ESP_LOGW(TAG, "Timeout esperando peso estable");
            s_weighing_active = false;

            app_event_t evt = {.type = APP_EVT_SCALE_TIMEOUT};
            app_events_post(&evt);
        }
    }
}

esp_err_t scale_task_start(void)
{
    esp_err_t ret = scale_uart_init(SCALE_BAUD_RATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1);
    if (ret != ESP_OK) {
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(scale_poll_task, "scale_poll", 4096, NULL, 4, NULL);
    return (task_ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
