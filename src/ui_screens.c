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

static lv_obj_t *s_label_keypad_display;
static lv_obj_t *s_btnmatrix_keypad;

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
    set_widget_visible(s_btnmatrix_keypad, false);
    set_widget_visible(s_label_keypad_display, false);
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

    bsp_display_unlock();

    ui_show_idle();
}

void ui_show_idle(void)
{
    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title("Esperando articulo");
    lv_label_set_text(s_label_detail, "Acerque el tag NFC de la caja al lector");
    set_widget_visible(s_btn_restart, true);
    set_widget_visible(s_btn_usb_mode, true);
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

static int s_wait_used_unidades_totales = 0;
static float s_wait_used_peso_total_g = 0.0f;

/* 3 botones cuadrados en una sola fila, ocupando todo el ancho, para dejar
 * el maximo de alto posible al texto (que en esta pantalla puede llegar a
 * 5 lineas: peso total/unidades, instrucciones, y ultima lectura). */
static void layout_bottom_row_3_buttons(void)
{
    lv_obj_set_size(s_btn_confirm, 146, 100);
    lv_obj_align(s_btn_confirm, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    lv_obj_set_size(s_btn_retry, 146, 100);
    lv_obj_align(s_btn_retry, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_set_size(s_btn_cancel, 146, 100);
    lv_obj_align(s_btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

void ui_show_wait_weight_used(const char *descripcion, int unidades_totales, float peso_total_g)
{
    char titulo[80];
    sanitize_for_display(descripcion, titulo, sizeof(titulo));

    s_wait_used_unidades_totales = unidades_totales;
    s_wait_used_peso_total_g = peso_total_g;

    char buf[400];
    snprintf(buf, sizeof(buf),
             "Peso total: %.1f g   Unidades totales: %d\n"
             "Retire los utiles NUEVOS (puede hacerlo en varias tandas).\n"
             "Pulse Confirmar cuando este listo.\n\n"
             "Esperando primera lectura...", peso_total_g, unidades_totales);

    bsp_display_lock(0);
    hide_interactive_widgets();
    set_title(titulo);
    lv_label_set_text(s_label_detail, buf);

    layout_bottom_row_3_buttons();
    lv_label_set_text(s_label_btn_retry, "Repetir pesada total");
    set_widget_visible(s_btn_confirm, true);
    set_widget_visible(s_btn_retry, true);
    set_widget_visible(s_btn_cancel, true);

    bsp_display_unlock();
}

void ui_update_wait_weight_reading(float weight_g)
{
    char buf[400];
    snprintf(buf, sizeof(buf),
             "Peso total: %.1f g   Unidades totales: %d\n"
             "Retire los utiles NUEVOS (puede hacerlo en varias tandas).\n"
             "Pulse Confirmar cuando este listo.\n\n"
             "Ultimo peso leido: %.1f g",
             s_wait_used_peso_total_g, s_wait_used_unidades_totales, weight_g);

    bsp_display_lock(0);
    lv_label_set_text(s_label_detail, buf);
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
     * columna de botones de la derecha ni depender de cuantas lineas ocupe
     * el titulo), teclado grande debajo, Confirmar/Cancelar apilados a la
     * derecha en vez de robarle altura al teclado. */
    lv_obj_set_width(s_label_title, 340);
    lv_obj_set_style_text_font(s_label_title, &lv_font_montserrat_20, 0);
    lv_obj_align(s_label_title, LV_ALIGN_TOP_LEFT, 10, 8);
    set_title(titulo);

    set_widget_visible(s_label_detail, false);

    lv_label_set_text(s_label_keypad_display, "0");
    lv_obj_set_width(s_label_keypad_display, 340);
    lv_obj_align(s_label_keypad_display, LV_ALIGN_TOP_LEFT, 10, 62);
    set_widget_visible(s_label_keypad_display, true);

    lv_obj_set_size(s_btnmatrix_keypad, 340, 200);
    lv_obj_align(s_btnmatrix_keypad, LV_ALIGN_TOP_LEFT, 10, 108);
    set_widget_visible(s_btnmatrix_keypad, true);

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
