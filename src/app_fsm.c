#include "app_fsm.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"

#include "app_events.h"
#include "ui_screens.h"
#include "csv_master.h"
#include "csv_inventory.h"
#include "scale_task.h"
#include "esp_bsp.h"
#include "display.h"
#include "usb_msc.h"

static const char *TAG = "app_fsm";

/* Atenuacion de pantalla por inactividad: sin toques, tags leidos ni
 * lecturas de peso durante este tiempo, se baja el brillo. Cualquier
 * actividad lo restaura al instante. */
#define SCREEN_DIM_TIMEOUT_MS  (2 * 60 * 1000)
#define SCREEN_DIM_BRIGHTNESS  5
#define SCREEN_DIM_CHECK_MS    1000

static bool s_screen_dimmed = false;

/* Constante para generar el codigo de un material nuevo: codigo = 1300000000
 * + los ultimos digitos (hasta 8) que teclee el operario. Da siempre un
 * codigo de 10 digitos empezando por "13". */
#define REG_CODIGO_BASE 1300000000L
#define REG_CODIGO_MAX_DIGITS 8

/* Al pesar la caja completa (primera pesada del flujo), se ignoran lecturas
 * por debajo de este umbral: son ruido/tara mientras la caja aun no esta
 * bien asentada en la bascula, no un peso real. */
#define SCALE_MIN_TOTAL_WEIGHT_G 5.0f

/* v2: mientras se retiran utiles nuevos, si la bascula vuelve a este umbral
 * (caja retirada por completo) se toma como disparador para terminar el
 * flujo automaticamente, sin necesidad de pulsar Confirmar. */
#define SCALE_ZERO_THRESHOLD_G 0.5f

static const char *MSG_REG_WEIGH_TOTAL = "Material nuevo: coloque la caja completa y espere...";

typedef enum {
    APP_STATE_IDLE,
    APP_STATE_WAIT_WEIGHT_TOTAL,
    APP_STATE_WAIT_WEIGHT_USED,
    APP_STATE_ERROR_DISMISSABLE,  /* duplicado / codigo ya existente -> Aceptar vuelve a IDLE (o a reintroducir codigo) */
    APP_STATE_DUPLICATE_CONFIRM,  /* codigo ya en inventario.csv -> Aceptar vuelve a IDLE, Sobrescribir repite el flujo */
    APP_STATE_TIMEOUT_WEIGHT,     /* timeout de bascula -> Reintentar re-arma la misma pesada */
    APP_STATE_INCONSISTENT_WEIGHT, /* usadas > totales -> Reintentar re-arma la 2a pesada */
    APP_STATE_CONFIRM_WEIGHT,     /* peso recien detectado, pendiente de Confirmar/Repetir pesada */

    /* Alta de material nuevo (tag no encontrado en datos_maestros) */
    APP_STATE_REG_WAIT_WEIGHT_TOTAL, /* pesando la caja completa */
    APP_STATE_REG_ENTER_CODE,        /* teclado: ultimos digitos del codigo */
    APP_STATE_REG_CODE_DUPLICATE,    /* el codigo generado ya existe -> Aceptar vuelve a pedirlo */
    APP_STATE_REG_ENTER_UNITS,       /* teclado: unidades totales (entero, manual) */
    APP_STATE_REG_WAIT_TARE,         /* pesando la caja vacia */
    APP_STATE_REG_ENTER_CALIBRE,     /* teclado decimal: calibre */
    APP_STATE_REG_ENTER_CABEZA,      /* teclado decimal: cabeza */

    APP_STATE_USB_MODE, /* SD expuesta por USB; solo se sale reiniciando */
} app_state_t;

typedef struct {
    char codigo[MASTER_CODIGO_MAX_LEN];
    char descripcion[MASTER_DESC_MAX_LEN];
    float tara_caja;
    float peso_unitario;
    int unidades_totales;
    float peso_total_g;                /* peso bruto de la caja completa, para mostrarlo en pantalla */
    bool overwrite_duplicate;         /* true si se confirmo sobrescribir un codigo ya en inventario.csv */
    app_state_t pending_weight_state; /* a que pesada volver tras un timeout o al confirmar/repetir */
    float pending_weight_g;           /* peso detectado, pendiente de confirmar (REG_*) */
    bool tiene_lectura_usados;        /* ya llego alguna lectura de fondo en WAIT_WEIGHT_USED */
    bool tiene_lectura_tara;          /* ya llego alguna lectura de fondo en REG_WAIT_TARE */

    /* Campos temporales, solo usados durante el alta de un material nuevo */
    char reg_uid[MASTER_UID_MAX_LEN];
    float reg_peso_total;
    float reg_calibre;
} process_ctx_t;

static app_state_t s_state = APP_STATE_IDLE;
static process_ctx_t s_ctx;

/* Recoge los ultimos articulos registrados (codigo->descripcion via
 * datos_maestros) y refresca la pantalla de reposo con esa tabla. */
static void go_idle(void)
{
    s_state = APP_STATE_IDLE;
    scale_cancel_weighing();
    memset(&s_ctx, 0, sizeof(s_ctx));

    csv_inventory_recent_t recent[CSV_INVENTORY_RECENT_MAX];
    size_t recent_count = csv_inventory_get_recent(recent, CSV_INVENTORY_RECENT_MAX);

    ui_recent_item_t ui_items[UI_RECENT_MAX];
    for (size_t i = 0; i < recent_count; i++) {
        const master_item_t *item = csv_master_find_by_codigo(recent[i].codigo);
        if (item) {
            strlcpy(ui_items[i].descripcion, item->descripcion, sizeof(ui_items[i].descripcion));
        } else {
            strlcpy(ui_items[i].descripcion, recent[i].codigo, sizeof(ui_items[i].descripcion));
        }
        ui_items[i].unidades_nuevas = recent[i].unidades_nuevas;
        ui_items[i].unidades_usadas = recent[i].unidades_usadas;
    }
    ui_show_idle(ui_items, recent_count);
}

static void finish_used_weighing(float weight_g);

/* Arma la pesada aplicando el filtro de peso minimo solo cuando el estado
 * de destino es una pesada de "caja completa" (normal o de alta de
 * material nuevo); el resto de pesadas (tara, usados) no se filtran. */
static void start_weighing_for_state(app_state_t target_state)
{
    float min_g = (target_state == APP_STATE_WAIT_WEIGHT_TOTAL ||
                    target_state == APP_STATE_REG_WAIT_WEIGHT_TOTAL)
                       ? SCALE_MIN_TOTAL_WEIGHT_G
                       : 0.0f;
    scale_request_weighing(min_g);
}

/* Igual que start_weighing_for_state(), pero sin reiniciar la ventana de
 * estabilidad ya acumulada: para los re-armados propios de la vigilancia
 * en segundo plano (WAIT_WEIGHT_USED, REG_WAIT_TARE), donde un reinicio en
 * cada ciclo haria parpadear el indicador de estable aunque el peso real
 * no cambie. */
static void continue_weighing_for_state(app_state_t target_state)
{
    float min_g = (target_state == APP_STATE_WAIT_WEIGHT_TOTAL ||
                    target_state == APP_STATE_REG_WAIT_WEIGHT_TOTAL)
                       ? SCALE_MIN_TOTAL_WEIGHT_G
                       : 0.0f;
    scale_continue_weighing(min_g);
}

/* Marca actividad "no tactil" (tag leido, peso detectado) para que la
 * pantalla no se atenue mientras hay progreso real aunque nadie la toque. */
static void mark_activity(void)
{
    bsp_display_lock(0);
    lv_disp_trig_activity(NULL);
    bsp_display_unlock();
}

/* Comprueba el tiempo de inactividad (toques + mark_activity()) y atenua o
 * restaura el brillo de la pantalla en consecuencia. */
static void check_screen_dim(void)
{
    bsp_display_lock(0);
    uint32_t idle_ms = lv_disp_get_inactive_time(NULL);
    bsp_display_unlock();

    if (!s_screen_dimmed && idle_ms >= SCREEN_DIM_TIMEOUT_MS) {
        bsp_display_brightness_set(SCREEN_DIM_BRIGHTNESS);
        s_screen_dimmed = true;
    } else if (s_screen_dimmed && idle_ms < SCREEN_DIM_TIMEOUT_MS) {
        bsp_display_brightness_set(100);
        s_screen_dimmed = false;
    }
}

static int units_from_weight(float peso_bruto_g)
{
    float peso_neto = peso_bruto_g - s_ctx.tara_caja;
    if (peso_neto < 0.0f) {
        peso_neto = 0.0f;
    }
    if (s_ctx.peso_unitario <= 0.0f) {
        return 0;
    }
    return (int)lroundf(peso_neto / s_ctx.peso_unitario);
}

static void handle_nfc_tag(const char *uid_hex)
{
    mark_activity();

    if (s_state != APP_STATE_IDLE) {
        ESP_LOGD(TAG, "Tag ignorado, proceso en curso (uid=%s)", uid_hex);
        return;
    }

    const master_item_t *item = csv_master_find_by_uid(uid_hex);
    if (!item) {
        /* Material desconocido: en vez de error, arrancamos el alta guiada */
        ESP_LOGI(TAG, "UID no encontrado, iniciando alta de material nuevo: %s", uid_hex);
        memset(&s_ctx, 0, sizeof(s_ctx));
        strlcpy(s_ctx.reg_uid, uid_hex, sizeof(s_ctx.reg_uid));

        s_state = APP_STATE_REG_WAIT_WEIGHT_TOTAL;
        ui_show_description_and_wait_weight(MSG_REG_WEIGH_TOTAL);
        start_weighing_for_state(s_state);
        return;
    }

    if (csv_inventory_has_codigo(item->codigo)) {
        ESP_LOGW(TAG, "Codigo duplicado: %s", item->codigo);
        memset(&s_ctx, 0, sizeof(s_ctx));
        strlcpy(s_ctx.codigo, item->codigo, sizeof(s_ctx.codigo));
        strlcpy(s_ctx.descripcion, item->descripcion, sizeof(s_ctx.descripcion));
        s_ctx.tara_caja = item->tara_caja;
        s_ctx.peso_unitario = item->peso_unitario;

        s_state = APP_STATE_DUPLICATE_CONFIRM;
        ui_show_duplicate_warning(item->descripcion);
        return;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    strlcpy(s_ctx.codigo, item->codigo, sizeof(s_ctx.codigo));
    strlcpy(s_ctx.descripcion, item->descripcion, sizeof(s_ctx.descripcion));
    s_ctx.tara_caja = item->tara_caja;
    s_ctx.peso_unitario = item->peso_unitario;

    s_state = APP_STATE_WAIT_WEIGHT_TOTAL;
    ui_show_description_and_wait_weight(s_ctx.descripcion);
    start_weighing_for_state(s_state);
}

/* Termina el alta de un material nuevo: guarda la entrada en datos_maestros
 * (RAM + fichero) y continua con el tramo final del flujo habitual
 * (confirmar retirada de nuevos -> segunda pesada), reutilizando s_ctx
 * exactamente igual que si el articulo ya existiera de antes. */
static void finish_new_material_registration(void)
{
    master_item_t nuevo = {0};
    strlcpy(nuevo.uid_nfc, s_ctx.reg_uid, sizeof(nuevo.uid_nfc));
    strlcpy(nuevo.codigo, s_ctx.codigo, sizeof(nuevo.codigo));
    strlcpy(nuevo.descripcion, s_ctx.descripcion, sizeof(nuevo.descripcion));
    nuevo.tara_caja = s_ctx.tara_caja;
    nuevo.peso_unitario = s_ctx.peso_unitario;

    if (csv_master_append(&nuevo) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo guardar el material nuevo en datos_maestros.csv");
    }

    s_state = APP_STATE_WAIT_WEIGHT_USED;
    s_ctx.tiene_lectura_usados = false;
    s_ctx.peso_total_g = s_ctx.reg_peso_total;
    ui_show_wait_weight_used(s_ctx.descripcion, s_ctx.unidades_totales, s_ctx.peso_total_g);
    start_weighing_for_state(s_state);
}

static void handle_scale_stable(float weight_g)
{
    mark_activity();

    switch (s_state) {
    case APP_STATE_WAIT_WEIGHT_TOTAL:
        /* Articulo ya conocido: se acepta automaticamente, sin confirmar, y
         * se pasa directo a vigilar la retirada de utiles nuevos. */
        s_ctx.unidades_totales = units_from_weight(weight_g);
        ESP_LOGI(TAG, "Unidades totales: %d", s_ctx.unidades_totales);
        s_state = APP_STATE_WAIT_WEIGHT_USED;
        s_ctx.tiene_lectura_usados = false;
        s_ctx.peso_total_g = weight_g;
        ui_show_wait_weight_used(s_ctx.descripcion, s_ctx.unidades_totales, s_ctx.peso_total_g);
        start_weighing_for_state(s_state);
        break;

    case APP_STATE_WAIT_WEIGHT_USED:
        /* v2 "sin botones": si la bascula se queda a 0 (caja retirada por
         * completo), se usa el ultimo peso estable ya visto como resultado
         * final, igual que si se hubiera pulsado Confirmar justo antes de
         * levantar la caja. Si aun no hay ninguna lectura previa (caja
         * levantada nada mas empezar, antes de estabilizar), se ignora y
         * se sigue esperando. Confirmar sigue disponible como respaldo
         * manual en todo momento. */
        if (fabsf(weight_g) <= SCALE_ZERO_THRESHOLD_G) {
            if (s_ctx.tiene_lectura_usados) {
                finish_used_weighing(s_ctx.pending_weight_g);
            } else {
                continue_weighing_for_state(APP_STATE_WAIT_WEIGHT_USED);
            }
            break;
        }

        /* Vigilancia en segundo plano: se actualiza el valor mostrado pero
         * no se avanza de estado hasta que el operario pulse Confirmar (o
         * la bascula llegue a 0), pudiendo retirar los utiles nuevos en
         * varias tandas. Se vuelve a armar para seguir mirando (sin
         * reiniciar la ventana, para que el indicador de estable no
         * parpadee si el peso no ha cambiado). */
        s_ctx.pending_weight_g = weight_g;
        s_ctx.tiene_lectura_usados = true;
        continue_weighing_for_state(APP_STATE_WAIT_WEIGHT_USED);
        break;

    case APP_STATE_REG_WAIT_WEIGHT_TOTAL:
        s_ctx.pending_weight_g = weight_g;
        s_ctx.pending_weight_state = s_state;
        s_state = APP_STATE_CONFIRM_WEIGHT;
        ui_show_confirm_weight("Peso total (material nuevo)", weight_g);
        break;

    case APP_STATE_REG_WAIT_TARE:
        /* Igual que WAIT_WEIGHT_USED: vigilancia en segundo plano sin
         * avanzar de estado, para dar tiempo real a vaciar la caja en vez
         * de aceptar el primer peso estable (que normalmente es aun el de
         * la caja llena, recien salida del paso anterior). */
        s_ctx.pending_weight_g = weight_g;
        s_ctx.tiene_lectura_tara = true;
        ui_update_wait_tare_reading(weight_g);
        continue_weighing_for_state(APP_STATE_REG_WAIT_TARE);
        break;

    default:
        ESP_LOGD(TAG, "Evento de peso estable ignorado en estado %d", (int)s_state);
        break;
    }
}

/* Lectura "en vivo" (cada muestra, estable o no) mientras se retiran
 * utiles nuevos: solo actualiza el feedback en pantalla (LED y
 * nuevas/usadas en grande); no cambia de estado ni guarda nada. Nuevas y
 * usadas siempre suman el total, para que se vea de un vistazo que el
 * inventario cuadra. */
static void handle_scale_reading(float weight_g, bool stable)
{
    if (s_state != APP_STATE_WAIT_WEIGHT_USED) {
        return;
    }

    int unidades_nuevas = s_ctx.unidades_totales - units_from_weight(weight_g);
    if (unidades_nuevas < 0) {
        unidades_nuevas = 0;
    } else if (unidades_nuevas > s_ctx.unidades_totales) {
        unidades_nuevas = s_ctx.unidades_totales;
    }
    int unidades_usadas = s_ctx.unidades_totales - unidades_nuevas;

    ui_update_wait_weight_live(unidades_nuevas, unidades_usadas, stable);
}

static void handle_scale_timeout(void)
{
    if (s_state == APP_STATE_WAIT_WEIGHT_USED || s_state == APP_STATE_REG_WAIT_TARE) {
        /* No interrumpir al operario mientras retira utiles/vacia la caja
         * en varias tandas: se sigue vigilando en segundo plano sin
         * mostrar error. */
        ESP_LOGD(TAG, "Timeout de bascula en estado %d, se sigue vigilando", (int)s_state);
        continue_weighing_for_state(s_state);
        return;
    }

    if (s_state == APP_STATE_WAIT_WEIGHT_TOTAL || s_state == APP_STATE_REG_WAIT_WEIGHT_TOTAL) {
        s_ctx.pending_weight_state = s_state;
        s_state = APP_STATE_TIMEOUT_WEIGHT;
        ui_show_error("No se pudo leer un peso estable. Reintente.");
    }
}

/* Calcula unidades usadas/nuevas a partir del peso ya confirmado por el
 * operario y guarda el resultado en inventario.csv. */
static void finish_used_weighing(float weight_g)
{
    scale_cancel_weighing(); /* dejar de vigilar en segundo plano */

    int unidades_usadas = units_from_weight(weight_g);
    ESP_LOGI(TAG, "Unidades usadas (confirmado por operario): %d", unidades_usadas);

    if (unidades_usadas > s_ctx.unidades_totales) {
        s_state = APP_STATE_INCONSISTENT_WEIGHT;
        ui_show_inconsistent_weight();
        return;
    }

    int unidades_nuevas = s_ctx.unidades_totales - unidades_usadas;
    if (s_ctx.overwrite_duplicate) {
        csv_inventory_append_or_update(s_ctx.codigo, unidades_nuevas, unidades_usadas);
    } else {
        csv_inventory_append(s_ctx.codigo, unidades_nuevas, unidades_usadas);
    }
    ui_show_result_ok(s_ctx.codigo, unidades_nuevas, unidades_usadas);

    vTaskDelay(pdMS_TO_TICKS(2500));
    go_idle();
}

static void handle_confirm_pressed(void)
{
    switch (s_state) {
    case APP_STATE_DUPLICATE_CONFIRM:
        s_ctx.overwrite_duplicate = true;
        s_state = APP_STATE_WAIT_WEIGHT_TOTAL;
        ui_show_description_and_wait_weight(s_ctx.descripcion);
        start_weighing_for_state(s_state);
        break;

    case APP_STATE_WAIT_WEIGHT_USED:
        if (!s_ctx.tiene_lectura_usados) {
            break; /* aun no ha llegado ninguna lectura, se ignora el toque */
        }
        finish_used_weighing(s_ctx.pending_weight_g);
        break;

    case APP_STATE_REG_WAIT_TARE:
        if (!s_ctx.tiene_lectura_tara) {
            break; /* aun no ha llegado ninguna lectura, se ignora el toque */
        }
        s_ctx.tara_caja = s_ctx.pending_weight_g;
        ESP_LOGI(TAG, "Alta material nuevo: tara caja = %.2f g", s_ctx.tara_caja);
        if (s_ctx.unidades_totales > 0) {
            s_ctx.peso_unitario = (s_ctx.reg_peso_total - s_ctx.tara_caja) / s_ctx.unidades_totales;
        } else {
            s_ctx.peso_unitario = 0.0f;
        }
        if (s_ctx.peso_unitario < 0.0f) {
            s_ctx.peso_unitario = 0.0f;
        }
        s_state = APP_STATE_REG_ENTER_CALIBRE;
        ui_show_keypad("Calibre (mm)", true, 6);
        break;

    case APP_STATE_CONFIRM_WEIGHT:
        switch (s_ctx.pending_weight_state) {
        case APP_STATE_REG_WAIT_WEIGHT_TOTAL:
            s_ctx.reg_peso_total = s_ctx.pending_weight_g;
            ESP_LOGI(TAG, "Alta material nuevo: peso total = %.2f g", s_ctx.pending_weight_g);
            s_state = APP_STATE_REG_ENTER_CODE;
            ui_show_keypad("Ultimos digitos del codigo (13 + estos digitos)", false, REG_CODIGO_MAX_DIGITS);
            break;

        default:
            break;
        }
        break;

    default:
        break;
    }
}

static void handle_keypad_confirmed(const char *text)
{
    switch (s_state) {
    case APP_STATE_REG_ENTER_CODE: {
        long digits = strtol(text, NULL, 10);
        if (digits < 0) {
            digits = 0;
        }
        long codigo_num = REG_CODIGO_BASE + digits;
        snprintf(s_ctx.codigo, sizeof(s_ctx.codigo), "%ld", codigo_num);

        const master_item_t *existing = csv_master_find_by_codigo(s_ctx.codigo);

        if (existing && existing->uid_nfc[0] != '\0') {
            /* Ya hay OTRO tag vinculado a este codigo: probable error de
             * tecleo, no dejar continuar con este codigo. */
            ESP_LOGW(TAG, "Codigo %s ya vinculado a otro tag (%s)", s_ctx.codigo, existing->uid_nfc);
            s_state = APP_STATE_REG_CODE_DUPLICATE;
            ui_show_error("Ese codigo ya esta vinculado a otro tag. Revise los digitos.");
            break;
        }

        if (existing) {
            /* Material precatalogado (fila en datos_maestros.csv sin tag
             * aun vinculado): ya se conocen descripcion/tara/peso_unitario,
             * asi que basta con vincular este tag y seguir con el tramo
             * final del flujo normal, igual que un articulo ya conocido. */
            strlcpy(s_ctx.descripcion, existing->descripcion, sizeof(s_ctx.descripcion));
            s_ctx.tara_caja = existing->tara_caja;
            s_ctx.peso_unitario = existing->peso_unitario;

            if (csv_master_link_uid(s_ctx.codigo, s_ctx.reg_uid) != ESP_OK) {
                ESP_LOGE(TAG, "No se pudo vincular el tag %s al codigo %s", s_ctx.reg_uid, s_ctx.codigo);
            }

            s_ctx.unidades_totales = units_from_weight(s_ctx.reg_peso_total);
            ESP_LOGI(TAG, "Tag vinculado a codigo existente %s, unidades totales=%d",
                     s_ctx.codigo, s_ctx.unidades_totales);

            s_state = APP_STATE_WAIT_WEIGHT_USED;
            s_ctx.tiene_lectura_usados = false;
            s_ctx.peso_total_g = s_ctx.reg_peso_total;
            ui_show_wait_weight_used(s_ctx.descripcion, s_ctx.unidades_totales, s_ctx.peso_total_g);
            start_weighing_for_state(s_state);
            break;
        }

        /* Codigo totalmente nuevo: sigue el alta completa (unidades, tara,
         * calibre, cabeza). */
        s_state = APP_STATE_REG_ENTER_UNITS;
        ui_show_keypad("Unidades totales en la caja", false, 5);
        break;
    }

    case APP_STATE_REG_ENTER_UNITS: {
        int unidades = (int)strtol(text, NULL, 10);
        if (unidades <= 0) {
            /* invalido, se vuelve a pedir sin perder el resto del contexto */
            ui_show_keypad("Unidades totales (numero mayor que 0)", false, 5);
            break;
        }
        s_ctx.unidades_totales = unidades;
        s_state = APP_STATE_REG_WAIT_TARE;
        s_ctx.tiene_lectura_tara = false;
        ui_show_wait_tare();
        start_weighing_for_state(s_state);
        break;
    }

    case APP_STATE_REG_ENTER_CALIBRE: {
        s_ctx.reg_calibre = strtof(text, NULL);
        s_state = APP_STATE_REG_ENTER_CABEZA;
        ui_show_keypad("Cabeza (mm)", true, 6);
        break;
    }

    case APP_STATE_REG_ENTER_CABEZA: {
        float cabeza = strtof(text, NULL);
        snprintf(s_ctx.descripcion, sizeof(s_ctx.descripcion),
                 "BULÓN BC Ø%.2fxØ%.2fx12º s/p 20181005", s_ctx.reg_calibre, cabeza);
        ESP_LOGI(TAG, "Alta material nuevo: codigo=%s descripcion=%s tara=%.2fg peso_unit=%.4fg",
                 s_ctx.codigo, s_ctx.descripcion, s_ctx.tara_caja, s_ctx.peso_unitario);
        finish_new_material_registration();
        break;
    }

    default:
        break;
    }
}

static void handle_retry_pressed(void)
{
    switch (s_state) {
    case APP_STATE_ERROR_DISMISSABLE:
    case APP_STATE_DUPLICATE_CONFIRM:
        go_idle();
        break;

    case APP_STATE_REG_CODE_DUPLICATE:
        s_state = APP_STATE_REG_ENTER_CODE;
        ui_show_keypad("Ultimos digitos del codigo (13 + estos digitos)", false, REG_CODIGO_MAX_DIGITS);
        break;

    case APP_STATE_WAIT_WEIGHT_USED:
        /* La pesada de la caja completa no fue correcta: se repite desde
         * cero, reutilizando descripcion/tara/peso_unitario ya conocidos. */
        s_state = APP_STATE_WAIT_WEIGHT_TOTAL;
        ui_show_description_and_wait_weight(s_ctx.descripcion);
        start_weighing_for_state(s_state);
        break;

    case APP_STATE_TIMEOUT_WEIGHT:
        s_state = s_ctx.pending_weight_state;
        switch (s_state) {
        case APP_STATE_WAIT_WEIGHT_TOTAL:
            ui_show_description_and_wait_weight(s_ctx.descripcion);
            break;
        case APP_STATE_REG_WAIT_WEIGHT_TOTAL:
            ui_show_description_and_wait_weight(MSG_REG_WEIGH_TOTAL);
            break;
        default:
            break;
        }
        start_weighing_for_state(s_state);
        break;

    case APP_STATE_CONFIRM_WEIGHT:
        /* "Repetir pesada": se vuelve al mismo paso de pesada sin perder
         * el resto del contexto ya recopilado. */
        s_state = s_ctx.pending_weight_state;
        switch (s_state) {
        case APP_STATE_REG_WAIT_WEIGHT_TOTAL:
            ui_show_description_and_wait_weight(MSG_REG_WEIGH_TOTAL);
            break;
        default:
            break;
        }
        start_weighing_for_state(s_state);
        break;

    case APP_STATE_INCONSISTENT_WEIGHT:
        s_state = APP_STATE_WAIT_WEIGHT_USED;
        s_ctx.tiene_lectura_usados = false;
        ui_show_wait_weight_used(s_ctx.descripcion, s_ctx.unidades_totales, s_ctx.peso_total_g);
        start_weighing_for_state(s_state);
        break;

    default:
        break;
    }
}

/* Activa el modo USB Mass Storage bajo demanda (solo desde reposo). No hay
 * vuelta atras por software: la unica salida es reiniciar el dispositivo. */
static void handle_usb_mode_pressed(void)
{
    if (s_state != APP_STATE_IDLE) {
        return;
    }

    scale_cancel_weighing();

    if (usb_msc_start() != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo activar el modo USB");
        s_state = APP_STATE_ERROR_DISMISSABLE;
        ui_show_error("Fallo al activar el modo USB. Reinicie e intentelo de nuevo.");
        return;
    }

    s_state = APP_STATE_USB_MODE;
    ui_show_usb_mode();
}

static void handle_cancel_pressed(void)
{
    switch (s_state) {
    case APP_STATE_WAIT_WEIGHT_TOTAL:
    case APP_STATE_WAIT_WEIGHT_USED:
    case APP_STATE_TIMEOUT_WEIGHT:
    case APP_STATE_INCONSISTENT_WEIGHT:
    case APP_STATE_CONFIRM_WEIGHT:
    case APP_STATE_REG_WAIT_WEIGHT_TOTAL:
    case APP_STATE_REG_ENTER_CODE:
    case APP_STATE_REG_CODE_DUPLICATE:
    case APP_STATE_REG_ENTER_UNITS:
    case APP_STATE_REG_WAIT_TARE:
    case APP_STATE_REG_ENTER_CALIBRE:
    case APP_STATE_REG_ENTER_CABEZA:
        go_idle();
        break;
    default:
        break;
    }
}

static void app_task(void *arg)
{
    app_event_t evt;
    while (1) {
        /* Timeout corto (en vez de portMAX_DELAY) para poder comprobar la
         * inactividad de pantalla aunque no llegue ningun evento. */
        BaseType_t got_evt = xQueueReceive(g_app_event_queue, &evt, pdMS_TO_TICKS(SCREEN_DIM_CHECK_MS));
        check_screen_dim();
        if (got_evt != pdTRUE) {
            continue;
        }
        switch (evt.type) {
        case APP_EVT_NFC_TAG:
            handle_nfc_tag(evt.data.nfc_tag.uid_hex);
            break;
        case APP_EVT_SCALE_STABLE:
            handle_scale_stable(evt.data.scale_stable.weight_g);
            break;
        case APP_EVT_SCALE_READING:
            handle_scale_reading(evt.data.scale_reading.weight_g, evt.data.scale_reading.stable);
            break;
        case APP_EVT_SCALE_TIMEOUT:
            handle_scale_timeout();
            break;
        case APP_EVT_UI_CONFIRM_PRESSED:
            handle_confirm_pressed();
            break;
        case APP_EVT_UI_CANCEL_PRESSED:
            handle_cancel_pressed();
            break;
        case APP_EVT_UI_RETRY_PRESSED:
            handle_retry_pressed();
            break;
        case APP_EVT_UI_KEYPAD_CONFIRMED:
            handle_keypad_confirmed(evt.data.keypad.text);
            break;
        case APP_EVT_UI_USB_MODE_PRESSED:
            handle_usb_mode_pressed();
            break;
        }
    }
}

void app_fsm_start(void)
{
    go_idle(); /* refresca la pantalla de reposo con el historial ya cargado de la SD */
    xTaskCreate(app_task, "app_fsm", 4096, NULL, 5, NULL);
}
