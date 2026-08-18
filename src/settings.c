#include "settings.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE "settings"

/* Tabla de definiciones. El orden tiene que coincidir con setting_id_t.
 * Los rangos son los propuestos durante el diseno; better ajustarlos aqui
 * si en el uso real conviene otro limite - es el unico sitio que hay que
 * tocar. */
static const settings_def_t s_defs[SETTING_COUNT] = {
    [SETTING_SCALE_WEIGHT_TOLERANCE_G] = {
        .id = SETTING_SCALE_WEIGHT_TOLERANCE_G,
        .nvs_key = "scl_tol_g",
        .group = "Báscula",
        .name = "Tolerancia de estabilidad",
        .unit = "g",
        .description =
            "Cuánto puede variar el peso entre dos lecturas seguidas para darlo por "
            "estable. Si la báscula tiembla o el entorno tiene vibración, súbelo para "
            "que no se quede esperando eternamente. Si lo notas demasiado permisivo "
            "(acepta pesos que aún se están asentando), bájalo.",
        .default_value = 0.5f,
        .min_value = 0.1f,
        .max_value = 5.0f,
        .decimals = 1,
    },
    [SETTING_SCALE_STABLE_WINDOW_SAMPLES] = {
        .id = SETTING_SCALE_STABLE_WINDOW_SAMPLES,
        .nvs_key = "scl_win_n",
        .group = "Báscula",
        .name = "Lecturas para dar por estable",
        .unit = "uds",
        .description =
            "Cuántas lecturas seguidas dentro de la tolerancia anterior hacen falta "
            "para confirmar el peso. Subirlo hace el pesaje más fiable pero más lento "
            "en confirmarse; bajarlo lo hace más rápido pero más sensible a que un "
            "temblor puntual se cuele como si fuera el peso real.",
        .default_value = 5.0f,
        .min_value = 2.0f,
        .max_value = 20.0f,
        .decimals = 0,
    },
    [SETTING_SCALE_STABILIZE_TIMEOUT_S] = {
        .id = SETTING_SCALE_STABILIZE_TIMEOUT_S,
        .nvs_key = "scl_stab_s",
        .group = "Báscula",
        .name = "Tiempo máximo de espera",
        .unit = "s",
        .description =
            "Cuánto espera el sistema a que el peso se estabilice antes de rendirse y "
            "avisar de que no ha podido leer un peso fiable. Súbelo si tu báscula es "
            "lenta o cuesta asentar la caja; bájalo para detectar antes un fallo real.",
        .default_value = 15.0f,
        .min_value = 5.0f,
        .max_value = 60.0f,
        .decimals = 0,
    },
    [SETTING_SCALE_MIN_TOTAL_WEIGHT_G] = {
        .id = SETTING_SCALE_MIN_TOTAL_WEIGHT_G,
        .nvs_key = "scl_min_g",
        .group = "Báscula",
        .name = "Peso mínimo caja llena",
        .unit = "g",
        .description =
            "Por debajo de este peso, una lectura se descarta como ruido mientras se "
            "espera colocar la caja completa. Súbelo si tarda en arrancar con tus "
            "cajas más ligeras; bájalo si necesitas contar cajas muy ligeras.",
        .default_value = 5.0f,
        .min_value = 0.0f,
        .max_value = 50.0f,
        .decimals = 1,
    },
    [SETTING_SCALE_ZERO_THRESHOLD_G] = {
        .id = SETTING_SCALE_ZERO_THRESHOLD_G,
        .nvs_key = "scl_zero_g",
        .group = "Báscula",
        .name = "Umbral de caja vacía",
        .unit = "g",
        .description =
            "Por debajo de este peso, la báscula se considera vacía y el sistema da "
            "el recuento por terminado solo, sin pulsar Confirmar. Súbelo si tu "
            "báscula no llega a marcar 0 exacto al vaciarse; bájalo si se te cierra "
            "el recuento antes de tiempo con la caja casi vacía.",
        .default_value = 0.5f,
        .min_value = 0.1f,
        .max_value = 5.0f,
        .decimals = 1,
    },
    [SETTING_UNIT_ROUNDING_TOLERANCE_PCT] = {
        .id = SETTING_UNIT_ROUNDING_TOLERANCE_PCT,
        .nvs_key = "unit_tol_pct",
        .group = "Báscula",
        .name = "Aviso de precisión en unidades",
        .unit = "%",
        .description =
            "Al retirar piezas, si el número de unidades se aleja de un entero más de "
            "este margen, se pone en naranja como aviso de que el peso unitario "
            "guardado podría estar mal calibrado. Subirlo reduce las falsas alarmas; "
            "bajarlo lo hace más estricto.",
        .default_value = 15.0f,
        .min_value = 5.0f,
        .max_value = 50.0f,
        .decimals = 0,
    },
    [SETTING_SCREEN_DIM_TIMEOUT_MIN] = {
        .id = SETTING_SCREEN_DIM_TIMEOUT_MIN,
        .nvs_key = "dim_time_min",
        .group = "Pantalla",
        .name = "Tiempo hasta atenuar",
        .unit = "min",
        .description =
            "Cuánto tiempo sin tocar la pantalla ni detectar actividad real (peso, "
            "tag) pasa antes de bajar el brillo para cuidarla. Cualquier toque o "
            "lectura lo restaura al instante.",
        .default_value = 2.0f,
        .min_value = 1.0f,
        .max_value = 30.0f,
        .decimals = 0,
    },
    [SETTING_SCREEN_DIM_BRIGHTNESS_PCT] = {
        .id = SETTING_SCREEN_DIM_BRIGHTNESS_PCT,
        .nvs_key = "dim_bright_pct",
        .group = "Pantalla",
        .name = "Brillo atenuado",
        .unit = "%",
        .description =
            "A qué nivel de brillo baja la pantalla tras el tiempo de inactividad. "
            "Cuanto más bajo, más se nota el ahorro, pero menos se ve de un vistazo "
            "sin tocarla.",
        .default_value = 5.0f,
        .min_value = 1.0f,
        .max_value = 50.0f,
        .decimals = 0,
    },
};

static float s_values[SETTING_COUNT];
static SemaphoreHandle_t s_mutex;

static esp_err_t nvs_init_once(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

const settings_def_t *settings_get_def(setting_id_t id)
{
    if (id >= SETTING_COUNT) {
        return NULL;
    }
    return &s_defs[id];
}

void settings_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    if (nvs_init_once() != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar NVS; se usan valores de fabrica sin guardar");
        for (size_t i = 0; i < SETTING_COUNT; i++) {
            s_values[i] = s_defs[i].default_value;
        }
        return;
    }

    nvs_handle_t handle;
    esp_err_t open_ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (open_ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo abrir NVS (%s); se usan valores de fabrica sin guardar",
                 esp_err_to_name(open_ret));
        for (size_t i = 0; i < SETTING_COUNT; i++) {
            s_values[i] = s_defs[i].default_value;
        }
        return;
    }

    for (size_t i = 0; i < SETTING_COUNT; i++) {
        float value = s_defs[i].default_value;
        size_t len = sizeof(value);
        esp_err_t ret = nvs_get_blob(handle, s_defs[i].nvs_key, &value, &len);
        if (ret != ESP_OK || len != sizeof(value)) {
            /* Primera vez, o clave nueva anadida en una version posterior:
             * se deja escrito el valor de fabrica para que el siguiente
             * arranque ya lo encuentre. */
            value = s_defs[i].default_value;
            nvs_set_blob(handle, s_defs[i].nvs_key, &value, sizeof(value));
        }
        s_values[i] = value;
    }
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Ajustes cargados desde NVS");
}

float settings_get(setting_id_t id)
{
    if (id >= SETTING_COUNT) {
        return 0.0f;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    float value = s_values[id];
    xSemaphoreGive(s_mutex);
    return value;
}

esp_err_t settings_set(setting_id_t id, float value)
{
    if (id >= SETTING_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    const settings_def_t *def = &s_defs[id];
    if (value < def->min_value || value > def->max_value) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_FAIL;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        ret = nvs_set_blob(handle, def->nvs_key, &value, sizeof(value));
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo guardar %s en NVS (%s)", def->nvs_key, esp_err_to_name(ret));
        return ret;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_values[id] = value;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Ajuste %s = %.2f%s", def->name, value, def->unit);
    return ESP_OK;
}

void settings_reset_defaults(void)
{
    for (size_t i = 0; i < SETTING_COUNT; i++) {
        settings_set((setting_id_t)i, s_defs[i].default_value);
    }
}
