/*
 * Ajustes de báscula/pantalla editables desde el menú "Ajustes" de la
 * pantalla, guardados en NVS (no en la SD: no se ven al sacar la tarjeta).
 *
 * Cada ajuste vive en su propia unidad "humana" (segundos, minutos,
 * porcentaje...), no en la unidad interna que usa el código (ms, ms,
 * fracción...) - la conversión la hace quien lee el valor, ver los
 * comentarios de settings_def[] en settings.c para el porqué de cada rango.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTING_SCALE_WEIGHT_TOLERANCE_G = 0,
    SETTING_SCALE_STABLE_WINDOW_SAMPLES,
    SETTING_SCALE_STABILIZE_TIMEOUT_S,
    SETTING_SCALE_MIN_TOTAL_WEIGHT_G,
    SETTING_SCALE_ZERO_THRESHOLD_G,
    SETTING_UNIT_ROUNDING_TOLERANCE_PCT,
    SETTING_SCREEN_DIM_TIMEOUT_MIN,
    SETTING_SCREEN_DIM_BRIGHTNESS_PCT,
    SETTING_COUNT,
} setting_id_t;

typedef struct {
    setting_id_t id;
    const char *nvs_key;      /* <=15 caracteres, limite de NVS */
    const char *group;        /* cabecera bajo la que se agrupa en la lista */
    const char *name;         /* nombre corto: fila de la lista y titulo */
    const char *unit;         /* sufijo mostrado: "g", "uds", "s", "min", "%" */
    const char *description;  /* texto explicativo de la pantalla intermedia */
    float default_value;
    float min_value;
    float max_value;
    int decimals;             /* 0 o 1 - cuantos decimales se muestran/tecleán */
} settings_def_t;

/**
 * @brief Carga los 8 ajustes desde NVS. Si es la primera vez (o falta
 * alguna clave), usa el valor de fabrica y lo deja escrito en NVS.
 * Llamar una unica vez al arrancar, antes de usar cualquier otra funcion
 * de este modulo.
 */
void settings_init(void);

/** @brief Definicion (nombre, rango, descripcion...) de un ajuste. */
const settings_def_t *settings_get_def(setting_id_t id);

/** @brief Valor actual, en la unidad "humana" indicada en su definicion. */
float settings_get(setting_id_t id);

/**
 * @brief Cambia un ajuste y lo persiste en NVS.
 *
 * @return ESP_OK si el valor estaba dentro de [min_value, max_value] y se
 * guardo; ESP_ERR_INVALID_ARG si estaba fuera de rango (no se toca nada).
 */
esp_err_t settings_set(setting_id_t id, float value);

/** @brief Vuelve a poner los 8 ajustes a su valor de fabrica. */
void settings_reset_defaults(void);

#ifdef __cplusplus
}
#endif
