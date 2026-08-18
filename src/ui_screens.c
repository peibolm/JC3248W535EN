#include "ui_screens.h"

#include <stdio.h>
#include <string.h>

#include "esp_bsp.h"
#include "esp_system.h"
#include "app_events.h"

static lv_obj_t *s_label_title;
static lv_obj_t *s_label_detail;

static lv_obj_t *s_btn_confirm;
static lv_obj_t *s_label_btn_confirm;

static lv_obj_t *s_btn_retry;
static lv_obj_t *s_label_btn_retry;

static lv_obj_t *s_btn_cancel;

static lv_obj_t *s_btn_restart;
static lv_obj_t *s_btn_usb_mode;
static lv_obj_t *s_btn_update_master;

static lv_obj_t *s_label_keypad_display;
static lv_obj_t *s_btnmatrix_keypad;

static lv_obj_t *s_table_recent;

/* Pantalla ui_show_wait_weight_used(): LED estable/inestable arriba a la
 * izquierda, instruccion corta bajo el titulo, y abajo 4 columnas
 * (cabecera pequena + numero grande) con el resumen de la pesada. */
static lv_obj_t *s_led_stable;
static lv_obj_t *s_label_retire_nuevos;
static lv_obj_t *s_cont_stats;
static lv_obj_t *s_stat_value_peso_total;
static lv_obj_t *s_stat_value_uds_totales;
static lv_obj_t *s_stat_value_nuevas;
static lv_obj_t *s_stat_value_usadas;

static bool s_keypad_active = false;
static bool s_keypad_allow_decimal = false;
static int s_keypad_max_len = 8;
static char s_keypad_buffer[APP_EVENT_KEYPAD_TEXT_MAX_LEN] = {0};

static const char *s_keypad_map[] = {
    "7", "8", "9", "\n",
    "4", "5", "6", "\n",
    "1", "2", "3", "\n",
    ".", "0", LV_SYMBOL_BACKSPACE, ""
};

static void confirm_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_keypad_active) {
        if (s_keypad_buffer[0] == '\0') {
            return; /* no dejar confirmar vacio */
        }
        app_event_t evt = {.type = APP_EVT_UI_KEYPAD_CONFIRMED};
        strlcpy(evt.data.keypad.text, s_keypad_buffer, sizeof(evt.data.keypad.text));
        app_events_post(&evt);
    } else {
        app_event_t evt = {.type = APP_EVT_UI_CONFIRM_PRESSED};
        app_events_post(&evt);
    }
}

static void retry_btn_event_cb(lv_event_t *e)
{
    (void)e;
    app_event_t evt = {.type = APP_EVT_UI_RETRY_PRESSED};
    app_events_post(&evt);
}

static void cancel_btn_event_cb(lv_event_t *e)
{
    (void)e;
    app_event_t evt = {.type = APP_EVT_UI_CANCEL_PRESSED};
    app_events_post(&evt);
}

/* Reinicio inmediato del ESP32. No pasa por app_fsm/app_events a propósito:
 * reiniciar es una acción de mantenimiento independiente del estado en el
 * que se encuentre el flujo de inventario en ese momento. */
static void restart_btn_event_cb(lv_event_t *e)
{
    (void)e;
    esp_restart();
}

static void update_master_btn_event_cb(lv_event_t *e)
{
    (void)e;
    app_event_t evt = {.type = APP_EVT_UI_UPDATE_MASTER_PRESSED};
    app_events_post(&evt);
}

static void usb_mode_btn_event_cb(lv_event_t *e)
{
    (void)e;
    app_event_t evt = {.type = APP_EVT_UI_USB_MODE_PRESSED};
    app_events_post(&evt);
}

static void keypad_btnmatrix_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    uint32_t id = lv_btnmatrix_get_selected_btn(obj);
    const char *txt = lv_btnmatrix_get_btn_text(obj, id);
    if (!txt) {
        return;
    }

    size_t len = strlen(s_keypad_buffer);
    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if (len > 0) {
            s_keypad_buffer[len - 1] = '\0';
        }
    } else if (strcmp(txt, ".") == 0) {
        if (s_keypad_allow_decimal && !strchr(s_keypad_buffer, '.') &&
            (int)len < s_keypad_max_len) {
            s_keypad_buffer[len] = '.';
            s_keypad_buffer[len + 1] = '\0';
        }
    } else {
        if ((int)len < s_keypad_max_len) {
            s_keypad_buffer[len] = txt[0];
            s_keypad_buffer[len + 1] = '\0';
        }
    }

    lv_label_set_text(s_label_keypad_display, s_keypad_buffer[0] ? s_keypad_buffer : "0");
}

/* La fuente LVGL compilada en este proyecto solo incluye ASCII basico, sin
 * tildes, "ñ" ni simbolos como "Ø"/"º" - se verian como recuadros en
 * blanco. Esta funcion los sustituye por su equivalente ASCII SOLO para
 * mostrar en pantalla; el texto real se guarda tal cual en el CSV. */
static void sanitize_for_display(const char *in, char *out, size_t out_size)
{
    size_t o = 0;
    size_t i = 0;
    while (in[i] != '\0' && o + 1 < out_size) {
        unsigned char c0 = (unsigned char)in[i];
        unsigned char c1 = (unsigned char)in[i + 1];
        char replacement = 0;
        bool consumed_two = false;

        if (c0 == 0xC3) {
            consumed_two = true;
            switch (c1) {
                case 0x81: replacement = 'A'; break; /* Á */
                case 0x89: replacement = 'E'; break; /* É */
                case 0x8D: replacement = 'I'; break; /* Í */
                case 0x93: replacement = 'O'; break; /* Ó */
                case 0x9A: replacement = 'U'; break; /* Ú */
                case 0x91: replacement = 'N'; break; /* Ñ */
                case 0x9C: replacement = 'U'; break; /* Ü */
                case 0x98: replacement = 0;   break; /* Ø -> se omite */
                case 0xA1: replacement = 'a'; break; /* á */
                case 0xA9: replacement = 'e'; break; /* é */
                case 0xAD: replacement = 'i'; break; /* í */
                case 0xB3: replacement = 'o'; break; /* ó */
                case 0xBA: replacement = 'u'; break; /* ú */
                case 0xB1: replacement = 'n'; break; /* ñ */
                case 0xBC: replacement = 'u'; break; /* ü */
                case 0xB8: replacement = 0;   break; /* ø -> se omite */
                default: consumed_two = false; break;
            }
        } else if (c0 == 0xC2) {
            consumed_two = true;
            switch (c1) {
                case 0xBA: replacement = 0; break;   /* º -> se omite */
                case 0xAA: replacement = 0; break;   /* ª -> se omite */
                case 0xBF: replacement = '?'; break; /* ¿ */
                case 0xA1: replacement = '!'; break; /* ¡ */
                default: consumed_two = false; break;
            }
        }

        if (consumed_two) {
            if (replacement) {
                out[o++] = replacement;
            }
            i += 2;
        } else {
            out[o++] = (char)c0;
            i += 1;
        }
    }
    out[o] = '\0';
}

static void set_widget_visible(lv_obj_t *obj, bool visible)
{
    if (visible) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Una columna del resumen de ui_show_wait_weight_used(): cabecera pequena
 * arriba, numero grande debajo. Devuelve el label del numero, que es el
 * unico que se actualiza despues. */
static lv_obj_t *create_stat_column(lv_obj_t *parent, const char *header_text)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 2, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_label_create(col);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_label_set_text(header, header_text);

    lv_obj_t *value = lv_label_create(col);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_28, 0);
    lv_label_set_text(value, "--");

    return value;
}

/* lv_obj_align_to() solo calcula la posicion en el momento en que se llama;
 * no vuelve a recalcularse sola si el titulo cambia despues de tamano
 * (p.ej. pasa de 1 a 2 lineas). Por eso el body se posiciona relativo al
 * titulo AQUI, cada vez que cambia el texto del titulo, en vez de solo una
 * vez en ui_screens_init() - si no, con un titulo de 2 lineas el body se
 * queda solapado con la segunda linea. */
static void set_title(const char *text)
{
    lv_label_set_text(s_label_title, text);
    lv_obj_align_to(s_label_detail, s_label_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
}

/* Oculta todos los widgets interactivos y restaura la posicion/tamano por
 * defecto de titulo/confirmar/cancelar (ui_show_keypad y ui_show_wait_*
 * los reubican de forma distinta para sus necesidades). Cada ui_show_*()
 * llama a esto primero y luego activa solo lo que necesita. */
static void hide_interactive_widgets(void)
{
    set_widget_visible(s_btn_confirm, false);
    set_widget_visible(s_btn_retry, false);
    set_widget_visible(s_btn_cancel, false);
    set_widget_visible(s_btn_restart, false);
    set_widget_visible(s_btn_usb_mode, false);
    set_widget_visible(s_btn_update_master, false);
    set_widget_visible(s_btnmatrix_keypad, false);
    set_widget_visible(s_label_keypad_display, false);
    set_widget_visible(s_table_recent, false);
    set_widget_visible(s_led_stable, false);
    set_widget_visible(s_label_retire_nuevos, false);
    set_widget_visible(s_cont_stats, false);
    set_widget_visible(s_label_detail, true);

    lv_label_set_text(s_label_btn_confirm, "Confirmar");
    lv_obj_set_size(s_btn_confirm, 240, 70);
    lv_obj_align(s_btn_confirm, LV_ALIGN_BOTTOM_MID, 0, -110);
    lv_obj_set_size(s_btn_retry, 240, 70);
    lv_obj_align(s_btn_retry, LV_ALIGN_BOTTOM_MID, 0, -110);
    lv_obj_set_size(s_btn_cancel, 240, 60);
    lv_obj_align(s_btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* ui_show_keypad() reposiciona/reduce el titulo para dejar sitio al
     * teclado; el resto de pantallas necesitan el titulo grande, centrado
     * y con el margen superior reducido de siempre. */
    lv_obj_set_width(s_label_title, LV_PCT(90));
    lv_obj_set_style_text_font(s_label_title, &lv_font_montserrat_28, 0);
    lv_obj_align(s_label_title, LV_ALIGN_TOP_MID, 0, 12);

    s_keypad_active = false;
}

void ui_screens_init(lv_disp_t *disp)
{
    bsp_display_lock(0);

    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_clean(scr);

    s_label_title = lv_label_create(scr);
    lv_obj_set_width(s_label_title, LV_PCT(90));
    lv_label_set_long_mode(s_label_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_label_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_label_title, &lv_font_montserrat_28, 0);
    lv_obj_align(s_label_title, LV_ALIGN_TOP_MID, 0, 12);

    s_label_detail = lv_label_create(scr);
    lv_obj_set_width(s_label_detail, LV_PCT(90));
    lv_label_set_long_mode(s_label_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_label_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(s_label_detail, s_label_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    s_label_keypad_display = lv_label_create(scr);
    lv_obj_set_style_text_font(s_label_keypad_display, &lv_font_montserrat_28, 0);
    lv_obj_align_to(s_label_keypad_display, s_label_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    s_btnmatrix_keypad = lv_btnmatrix_create(scr);
    lv_btnmatrix_set_map(s_btnmatrix_keypad, s_keypad_map);
    lv_obj_set_size(s_btnmatrix_keypad, 300, 150);
    lv_obj_align(s_btnmatrix_keypad, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_add_event_cb(s_btnmatrix_keypad, keypad_btnmatrix_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Tabla de los ultimos articulos registrados, solo visible en reposo. */
    s_table_recent = lv_table_create(scr);
    lv_table_set_col_cnt(s_table_recent, 3);
    lv_table_set_col_width(s_table_recent, 0, 260);
    lv_table_set_col_width(s_table_recent, 1, 90);
    lv_table_set_col_width(s_table_recent, 2, 90);
    lv_obj_set_size(s_table_recent, 440, 190);
    lv_obj_align(s_table_recent, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_font(s_table_recent, &lv_font_montserrat_14, 0);

    /* Pantalla ui_show_wait_weight_used(): LED arriba a la izquierda,
     * instruccion corta bajo el titulo (reposicionada cada vez que se
     * muestra la pantalla), y resumen de la pesada en 4 columnas abajo. */
    s_led_stable = lv_led_create(scr);
    lv_obj_set_size(s_led_stable, 26, 26);
    lv_obj_align(s_led_stable, LV_ALIGN_TOP_LEFT, 10, 10);

    s_label_retire_nuevos = lv_label_create(scr);
    lv_obj_set_style_text_font(s_label_retire_nuevos, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_label_retire_nuevos, "RETIRE NUEVOS");

    s_cont_stats = lv_obj_create(scr);
    lv_obj_set_size(s_cont_stats, 460, 140);
    lv_obj_align(s_cont_stats, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_bg_opa(s_cont_stats, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cont_stats, 0, 0);
    lv_obj_set_style_pad_all(s_cont_stats, 0, 0);
    lv_obj_clear_flag(s_cont_stats, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_cont_stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_cont_stats, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_stat_value_peso_total = create_stat_column(s_cont_stats, "Peso total");
    s_stat_value_uds_totales = create_stat_column(s_cont_stats, "Uds totales");
    s_stat_value_nuevas = create_stat_column(s_cont_stats, "Nuevas");
    s_stat_value_usadas = create_stat_column(s_cont_stats, "Usadas");

    s_btn_confirm = lv_btn_create(scr);
    lv_obj_add_event_cb(s_btn_confirm, confirm_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_label_btn_confirm = lv_label_create(s_btn_confirm);
    lv_label_set_text(s_label_btn_confirm, "Confirmar");
    lv_obj_center(s_label_btn_confirm);

    s_btn_retry = lv_btn_create(scr);
    lv_obj_add_event_cb(s_btn_retry, retry_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_label_btn_retry = lv_label_create(s_btn_retry);
    lv_label_set_text(s_label_btn_retry, "Aceptar");
    lv_obj_center(s_label_btn_retry);

    s_btn_cancel = lv_btn_create(scr);
    lv_obj_set_style_bg_color(s_btn_cancel, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(s_btn_cancel, cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_btn_cancel = lv_label_create(s_btn_cancel);
    lv_label_set_text(label_btn_cancel, "Cancelar");
    lv_obj_center(label_btn_cancel);

    /* Pequeño y discreto, en la esquina: recuperación/mantenimiento sin
     * necesitar acceso físico a ningún botón una vez encapsulado. */
    s_btn_restart = lv_btn_create(scr);
    lv_obj_set_size(s_btn_restart, 90, 36);
    lv_obj_align(s_btn_restart, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_style_bg_color(s_btn_restart, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_add_event_cb(s_btn_restart, restart_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_btn_restart = lv_label_create(s_btn_restart);
    lv_label_set_text(label_btn_restart, "Reiniciar");
    lv_obj_set_style_text_font(label_btn_restart, &lv_font_montserrat_12, 0);
    lv_obj_center(label_btn_restart);

    /* Igual de discreto, en la esquina opuesta: entra en modo USB bajo
     * demanda (solo disponible en reposo). */
    s_btn_usb_mode = lv_btn_create(scr);
    lv_obj_set_size(s_btn_usb_mode, 90, 36);
    lv_obj_align(s_btn_usb_mode, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_set_style_bg_color(s_btn_usb_mode, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_add_event_cb(s_btn_usb_mode, usb_mode_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_btn_usb_mode = lv_label_create(s_btn_usb_mode);
    lv_label_set_text(label_btn_usb_mode, "Modo USB");
    lv_obj_set_style_text_font(label_btn_usb_mode, &lv_font_montserrat_12, 0);
    lv_obj_center(label_btn_usb_mode);

    /* Solo visible en ui_show_wait_weight_used(): esquina superior derecha,
     * libre ahi (el LED de estable ocupa la izquierda y el titulo/RETIRE
     * NUEVOS el centro). Aborta el recuento en curso y lleva al mismo
     * tramo final que el alta de material nuevo (unidades -> tara ->
     * calibre -> cabeza), pero para CORREGIR la entrada ya existente en
     * vez de crear una nueva - ver handle_update_master_pressed() en
     * app_fsm.c. */
    s_btn_update_master = lv_btn_create(scr);
    lv_obj_set_size(s_btn_update_master, 150, 36);
    lv_obj_align(s_btn_update_master, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_style_bg_color(s_btn_update_master, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_add_event_cb(s_btn_update_master, update_master_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_btn_update_master = lv_label_create(s_btn_update_master);
    lv_label_set_text(label_btn_update_master, "Actualizar datos");
    lv_obj_set_style_text_font(label_btn_update_master, &lv_font_montserrat_12, 0);
    lv_obj_center(label_btn_update_master);

    bsp_display_unlock();

    ui_show_idle(NULL, 0);
}

void ui_show_idle(const ui_recent_item_t *items, size_t count)
{
    if (count > UI_RECENT_MAX) {
        count = UI_RECENT_MAX;
    }

    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Esperando articulo");
    lv_label_set_text(s_label_detail, "Acerque el tag NFC de la caja al lector");
    set_widget_visible(s_btn_restart, true);
    set_widget_visible(s_btn_usb_mode, true);

    lv_table_set_row_cnt(s_table_recent, (uint16_t)(count + 1));
    lv_table_set_cell_value(s_table_recent, 0, 0, "Descripcion");
    lv_table_set_cell_value(s_table_recent, 0, 1, "Nuevas");
    lv_table_set_cell_value(s_table_recent, 0, 2, "Usadas");
    for (size_t i = 0; i < count; i++) {
        char titulo[UI_RECENT_DESC_MAX_LEN];
        sanitize_for_display(items[i].descripcion, titulo, sizeof(titulo));
        char nuevas_str[8];
        char usadas_str[8];
        snprintf(nuevas_str, sizeof(nuevas_str), "%d", items[i].unidades_nuevas);
        snprintf(usadas_str, sizeof(usadas_str), "%d", items[i].unidades_usadas);
        lv_table_set_cell_value(s_table_recent, (uint16_t)(i + 1), 0, titulo);
        lv_table_set_cell_value(s_table_recent, (uint16_t)(i + 1), 1, nuevas_str);
        lv_table_set_cell_value(s_table_recent, (uint16_t)(i + 1), 2, usadas_str);
    }
    set_widget_visible(s_table_recent, true);

    bsp_display_unlock();
}

void ui_show_error(const char *msg)
{
    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Aviso");
    lv_label_set_text(s_label_detail, msg);
    lv_label_set_text(s_label_btn_retry, "Aceptar");
    set_widget_visible(s_btn_retry, true);
    bsp_display_unlock();
}

void ui_show_duplicate_warning(const char *descripcion)
{
    char titulo[80];
    sanitize_for_display(descripcion, titulo, sizeof(titulo));

    char buf[256];
    snprintf(buf, sizeof(buf),
             "%s\n\nEste articulo ya esta registrado en el inventario.\n"
             "Puede sobrescribir el registro anterior o aceptar y volver.",
             titulo);

    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Articulo duplicado");
    lv_label_set_text(s_label_detail, buf);

    lv_label_set_text(s_label_btn_retry, "Aceptar");
    lv_obj_set_size(s_btn_retry, 150, 60);
    lv_obj_align(s_btn_retry, LV_ALIGN_BOTTOM_MID, 80, -80);
    set_widget_visible(s_btn_retry, true);

    lv_label_set_text(s_label_btn_confirm, "Sobrescribir");
    lv_obj_set_size(s_btn_confirm, 150, 60);
    lv_obj_align(s_btn_confirm, LV_ALIGN_BOTTOM_MID, -80, -80);
    set_widget_visible(s_btn_confirm, true);

    bsp_display_unlock();
}

void ui_show_tag_relink_warning(const char *codigo, const char *descripcion)
{
    char desc_buf[64];
    sanitize_for_display(descripcion, desc_buf, sizeof(desc_buf));

    /* codigo (31) + desc_buf (63) + texto fijo (~204) + nul caben de sobra
     * en 340; con 220 el compilador avisaba (-Werror=format-truncation) de
     * que en el peor caso (los dos campos al maximo) se podia truncar. */
    char buf[340];
    snprintf(buf, sizeof(buf),
             "Codigo %s\n%s\n\n"
             "Este codigo ya tiene OTRO tag NFC vinculado (se perdio o se\n"
             "cambio el tag fisico de esta caja?).\n\n"
             "Sobrescribir vincula ESTE tag a este codigo, sin tocar la\n"
             "descripcion ni los pesos ya guardados.",
             codigo, desc_buf);

    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Codigo ya vinculado a otro tag");
    lv_label_set_text(s_label_detail, buf);

    lv_label_set_text(s_label_btn_confirm, "Sobrescribir");
    set_widget_visible(s_btn_confirm, true);
    set_widget_visible(s_btn_cancel, true);

    bsp_display_unlock();
}

void ui_show_saving(void)
{
    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Guardando...");
    lv_label_set_text(s_label_detail,
        "Actualizando el registro de este codigo en la tarjeta.\n\n"
        "No apague el equipo ni retire la tarjeta.");
    bsp_display_unlock();
}

void ui_show_description_and_wait_weight(const char *descripcion)
{
    char titulo[80];
    sanitize_for_display(descripcion, titulo, sizeof(titulo));

    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title(titulo);
    lv_label_set_text(s_label_detail, "Coloque la caja completa en la bascula y espere...");
    set_widget_visible(s_btn_cancel, true);
    bsp_display_unlock();
}

/* 3 botones cuadrados en una sola fila, ocupando todo el ancho y con la
 * mitad de alto de lo habitual, para dejar el maximo de alto posible al
 * resumen de la pesada. */
static void layout_bottom_row_3_buttons(void)
{
    lv_obj_set_size(s_btn_confirm, 146, 50);
    lv_obj_align(s_btn_confirm, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    lv_obj_set_size(s_btn_retry, 146, 50);
    lv_obj_align(s_btn_retry, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_set_size(s_btn_cancel, 146, 50);
    lv_obj_align(s_btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

void ui_show_wait_weight_used(const char *descripcion, int unidades_totales, float peso_total_g)
{
    char titulo[80];
    sanitize_for_display(descripcion, titulo, sizeof(titulo));

    char peso_total_buf[16];
    snprintf(peso_total_buf, sizeof(peso_total_buf), "%.1f g", peso_total_g);
    char uds_totales_buf[12];
    snprintf(uds_totales_buf, sizeof(uds_totales_buf), "%d", unidades_totales);

    bsp_display_lock(0);
    hide_interactive_widgets();

    /* El titulo (descripcion del articulo, p.ej. "BULON BC OX.XXxOX.XXx12o
     * s/p 20181005") ocupa normalmente 2 lineas a los 90% de ancho por
     * defecto - y esa anchura por defecto se solapa con el boton
     * "Actualizar datos" de la esquina superior derecha. Se estrecha a una
     * franja segura entre el LED (izquierda) y el boton (derecha): por
     * construccion ninguna longitud de texto puede alcanzar el boton, sea
     * cual sea la descripcion. */
    lv_obj_set_width(s_label_title, 260);
    lv_obj_align(s_label_title, LV_ALIGN_TOP_LEFT, 50, 12);
    set_title(titulo);
    set_widget_visible(s_label_detail, false);

    lv_label_set_text(s_label_retire_nuevos, "RETIRE NUEVOS");
    lv_obj_align_to(s_label_retire_nuevos, s_label_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
    set_widget_visible(s_label_retire_nuevos, true);

    lv_led_set_color(s_led_stable, lv_palette_main(LV_PALETTE_GREY));
    lv_led_on(s_led_stable);
    set_widget_visible(s_led_stable, true);

    lv_label_set_text(s_stat_value_peso_total, peso_total_buf);
    lv_label_set_text(s_stat_value_uds_totales, uds_totales_buf);
    lv_label_set_text(s_stat_value_nuevas, "0.0");
    lv_label_set_text(s_stat_value_usadas, uds_totales_buf);
    /* El color de aviso (ui_update_wait_weight_live) se queda "pegado" en
     * el objeto LVGL entre pantallas: se resetea aqui para que un aviso
     * del articulo anterior no aparezca ya puesto antes de la primera
     * lectura de este. */
    lv_obj_set_style_text_color(s_stat_value_nuevas, lv_color_hex(0x202632), 0);
    lv_obj_set_style_text_color(s_stat_value_usadas, lv_color_hex(0x202632), 0);
    set_widget_visible(s_cont_stats, true);

    layout_bottom_row_3_buttons();
    lv_label_set_text(s_label_btn_retry, "Repetir pesada total");
    set_widget_visible(s_btn_confirm, true);
    set_widget_visible(s_btn_retry, true);
    set_widget_visible(s_btn_cancel, true);
    set_widget_visible(s_btn_update_master, true);

    bsp_display_unlock();
}

void ui_show_confirm_articulo(const char *codigo, const char *descripcion)
{
    char titulo[80];
    sanitize_for_display(descripcion, titulo, sizeof(titulo));

    char buf[64];
    snprintf(buf, sizeof(buf), "Codigo %s\n\n¿Es este el material que se esta contando?", codigo);

    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title(titulo);
    lv_label_set_text(s_label_detail, buf);

    layout_bottom_row_3_buttons();
    lv_label_set_text(s_label_btn_retry, "Revisar codigo");
    set_widget_visible(s_btn_confirm, true);
    set_widget_visible(s_btn_retry, true);
    set_widget_visible(s_btn_cancel, true);

    bsp_display_unlock();
}

void ui_update_wait_weight_live(float unidades_nuevas, float unidades_usadas, bool stable,
                                 bool peso_unitario_sospechoso)
{
    char nuevas_buf[12];
    snprintf(nuevas_buf, sizeof(nuevas_buf), "%.1f", unidades_nuevas);
    char usadas_buf[12];
    snprintf(usadas_buf, sizeof(usadas_buf), "%.1f", unidades_usadas);

    /* Naranja si el desvio respecto a un numero entero supera la
     * tolerancia (ver UNIT_ROUNDING_TOLERANCE en app_fsm.c) - aviso de que
     * el peso_unitario guardado podria estar mal, o de que hay algo raro
     * en la caja. Color normal en caso contrario (hay que fijarlo
     * explicitamente los dos casos: el label no tiene color propio, y sin
     * esto se quedaria "pegado" en naranja tras el primer aviso). */
    lv_color_t color = peso_unitario_sospechoso
        ? lv_palette_main(LV_PALETTE_ORANGE)
        : lv_color_hex(0x202632);

    bsp_display_lock(0);
    lv_label_set_text(s_stat_value_nuevas, nuevas_buf);
    lv_label_set_text(s_stat_value_usadas, usadas_buf);
    lv_obj_set_style_text_color(s_stat_value_nuevas, color, 0);
    lv_obj_set_style_text_color(s_stat_value_usadas, color, 0);
    lv_led_set_color(s_led_stable, stable ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED));
    bsp_display_unlock();
}

void ui_show_wait_tare(void)
{
    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Peso de tara (material nuevo)");
    lv_label_set_text(s_label_detail,
        "Vacie la caja (retire todos los utiles).\n"
        "Pulse Confirmar cuando este listo.\n\n"
        "Esperando primera lectura...");
    set_widget_visible(s_btn_confirm, true);
    set_widget_visible(s_btn_cancel, true);
    bsp_display_unlock();
}

void ui_update_wait_tare_reading(float weight_g)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "Vacie la caja (retire todos los utiles).\n"
             "Pulse Confirmar cuando este listo.\n\n"
             "Ultimo peso leido: %.1f g", weight_g);

    bsp_display_lock(0);
    lv_label_set_text(s_label_detail, buf);
    bsp_display_unlock();
}

void ui_show_inconsistent_weight(void)
{
    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Peso inconsistente");
    lv_label_set_text(s_label_detail, "Las unidades usadas superan al total. No se ha guardado nada.");
    lv_label_set_text(s_label_btn_retry, "Reintentar pesada");
    set_widget_visible(s_btn_retry, true);
    set_widget_visible(s_btn_cancel, true);
    bsp_display_unlock();
}

void ui_show_result_ok(const char *codigo, int unidades_nuevas, int unidades_usadas)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "Codigo %s\nNuevas: %d    Usadas: %d", codigo, unidades_nuevas, unidades_usadas);

    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Guardado en inventario");
    lv_label_set_text(s_label_detail, buf);
    bsp_display_unlock();
}

void ui_show_result_master_updated(const char *codigo, const char *descripcion, float tara_caja, float peso_unitario)
{
    char desc_buf[64];
    sanitize_for_display(descripcion, desc_buf, sizeof(desc_buf));

    /* Igual que en ui_show_tag_relink_warning(): con codigo+descripcion al
     * maximo mas el texto fijo y los 2 numeros, 192 se quedaba corto para
     * el peor caso (-Werror=format-truncation). */
    char buf[280];
    snprintf(buf, sizeof(buf),
             "Codigo %s\n%s\nTara: %.2f g    Peso unitario: %.4f g\n\n"
             "El recuento en curso NO se ha guardado.\n"
             "Vuelva a pasar el tag para inventariar.",
             codigo, desc_buf, tara_caja, peso_unitario);

    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Datos maestros actualizados");
    lv_label_set_text(s_label_detail, buf);
    bsp_display_unlock();
}

void ui_show_fatal_error(const char *msg)
{
    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Error");
    lv_label_set_text(s_label_detail, msg);
    bsp_display_unlock();
}

void ui_show_keypad(const char *titulo, bool permitir_decimal, int max_len)
{
    if (max_len >= APP_EVENT_KEYPAD_TEXT_MAX_LEN) {
        max_len = APP_EVENT_KEYPAD_TEXT_MAX_LEN - 1;
    }

    bsp_display_lock(0);
    hide_interactive_widgets();

    s_keypad_buffer[0] = '\0';
    s_keypad_allow_decimal = permitir_decimal;
    s_keypad_max_len = max_len;
    s_keypad_active = true;

    /* Layout propio de esta pantalla (480x320 apaisado): titulo y campo de
     * texto en una columna izquierda de ancho fijo (para no invadir la
     * columna de botones de la derecha), teclado grande debajo,
     * Confirmar/Cancelar apilados a la derecha en vez de robarle altura al
     * teclado.
     *
     * El valor tecleado y el teclado se anclan cada uno al borde inferior
     * REAL del elemento anterior (lv_obj_align_to), no a una coordenada
     * fija: dos de los titulos que usa esta pantalla ("Ultimos digitos del
     * codigo (13 + estos digitos)", "Unidades totales (numero mayor que
     * 0)") ocupan 2 lineas a 340px de ancho. Con coordenadas fijas el
     * margen entre titulo y valor dependeria de la metrica exacta de la
     * fuente - con anclaje es imposible que se solapen, sea cual sea el
     * texto. El alto del teclado se calcula igual, hasta 8px del borde
     * inferior real de pantalla (mismo margen que usa el resto de la app
     * para botones pegados al borde, ver p.ej. ui_show_confirm_weight()). */
    lv_obj_set_width(s_label_title, 340);
    lv_obj_set_style_text_font(s_label_title, &lv_font_montserrat_18, 0);
    lv_obj_align(s_label_title, LV_ALIGN_TOP_LEFT, 10, 6);
    set_title(titulo);

    set_widget_visible(s_label_detail, false);

    lv_label_set_text(s_label_keypad_display, "0");
    lv_obj_set_width(s_label_keypad_display, 340);
    lv_obj_align_to(s_label_keypad_display, s_label_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    set_widget_visible(s_label_keypad_display, true);

    lv_obj_set_width(s_btnmatrix_keypad, 340);
    lv_obj_align_to(s_btnmatrix_keypad, s_label_keypad_display, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    set_widget_visible(s_btnmatrix_keypad, true);

    lv_coord_t keypad_y = lv_obj_get_y(s_btnmatrix_keypad);
    lv_coord_t screen_h = lv_obj_get_height(lv_scr_act());
    lv_obj_set_height(s_btnmatrix_keypad, screen_h - keypad_y - 8);

    lv_obj_set_size(s_btn_confirm, 100, 80);
    lv_obj_align(s_btn_confirm, LV_ALIGN_TOP_RIGHT, -10, 60);
    set_widget_visible(s_btn_confirm, true);

    lv_obj_set_size(s_btn_cancel, 100, 80);
    lv_obj_align(s_btn_cancel, LV_ALIGN_TOP_RIGHT, -10, 150);
    set_widget_visible(s_btn_cancel, true);

    bsp_display_unlock();
}

void ui_show_confirm_weight(const char *titulo, float peso_g)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "Peso registrado: %.1f g\n\nEs correcto?", peso_g);

    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title(titulo);
    lv_label_set_text(s_label_detail, buf);
    lv_label_set_text(s_label_btn_retry, "Repetir pesada");

    lv_obj_set_size(s_btn_confirm, 150, 60);
    lv_obj_align(s_btn_confirm, LV_ALIGN_BOTTOM_MID, -80, -80);
    set_widget_visible(s_btn_confirm, true);

    lv_obj_set_size(s_btn_retry, 150, 60);
    lv_obj_align(s_btn_retry, LV_ALIGN_BOTTOM_MID, 80, -80);
    set_widget_visible(s_btn_retry, true);

    lv_obj_set_size(s_btn_cancel, 200, 50);
    lv_obj_align(s_btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -15);
    set_widget_visible(s_btn_cancel, true);

    bsp_display_unlock();
}

void ui_show_usb_mode(void)
{
    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Modo USB activo");
    lv_label_set_text(s_label_detail,
        "La tarjeta SD ya esta disponible como unidad USB en el PC.\n\n"
        "Pulse Reiniciar (arriba a la derecha) para volver\n"
        "al funcionamiento normal.");
    set_widget_visible(s_btn_restart, true);
    bsp_display_unlock();
}
