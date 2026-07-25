/*
 * Tarea que lee UART1 y aplica la lógica de estabilización de peso pedida:
 * en vez de depender de un bit "estable" de la trama, espera a que las
 * últimas lecturas se mantengan dentro de una tolerancia durante una
 * ventana de tiempo. Nunca toca LVGL: solo envía eventos a la cola de la app.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Inicializa UART1 y arranca la tarea de báscula (en reposo hasta que se
 *  pida una pesada con scale_request_weighing()). */
esp_err_t scale_task_start(void);

/** Arma una nueva ventana de estabilización: la tarea empezará a acumular
 *  muestras y, al estabilizarse (o agotar el timeout), enviará
 *  APP_EVT_SCALE_STABLE / APP_EVT_SCALE_TIMEOUT.
 *
 *  @param min_weight_g Lecturas con |peso| por debajo de este valor se
 *  ignoran por completo (ni cuentan para la ventana de estabilidad ni la
 *  reinician); util para descartar ruido/tara mientras aun no se ha
 *  colocado la caja en la bascula. Pasar 0.0f para no filtrar nada. */
void scale_request_weighing(float min_weight_g);

/** Aborta una pesada en curso sin emitir ningún evento. */
void scale_cancel_weighing(void);

#ifdef __cplusplus
}
#endif
