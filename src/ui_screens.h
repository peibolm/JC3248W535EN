/*
 * Pantallas LVGL de la app de inventario, sustituyen a lv_demo_widgets().
 * Solo app_fsm (y los propios callbacks de botón, que ya corren dentro de
 * la tarea LVGL) deben llamar a estas funciones.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Crea los widgets reutilizables sobre la pantalla activa de disp. */
void ui_screens_init(lv_disp_t *disp);

/** Pantalla de espera: "Acerque un articulo al lector". */
void ui_show_idle(void);

/** Pantalla de error genérica (código no encontrado, duplicado...) con
 *  botón para volver a esperar tag. */
void ui_show_error(const char *msg);

/**
 * @brief Articulo con codigo ya registrado en inventario.csv: ofrece
 * "Aceptar" (volver sin hacer nada) o "Sobrescribir" (repetir el proceso y
 * reemplazar la fila anterior de ese codigo en el inventario).
 */
void ui_show_duplicate_warning(const char *descripcion);

/** Muestra la descripción del artículo y pide colocarlo en la báscula. */
void ui_show_description_and_wait_weight(const char *descripcion);

/**
 * @brief Muestra el peso total y las unidades detectadas, y pide retirar
 * los útiles nuevos (puede hacerse en varias tandas) confirmando con el
 * botón táctil. Deja el botón Confirmar disponible desde el primer momento;
 * no hace falta esperar a que el peso se estabilice para poder pulsarlo.
 * Incluye también "Repetir pesada total" por si la pesada de la caja
 * completa no fue correcta.
 */
void ui_show_wait_weight_used(const char *descripcion, int unidades_totales, float peso_total_g);

/**
 * @brief Actualiza solo el texto de la última lectura de peso mostrada en
 * la pantalla de ui_show_wait_weight_used(), sin tocar los botones. Se
 * llama repetidamente en segundo plano mientras el operario decide cuándo
 * pulsar Confirmar.
 */
void ui_update_wait_weight_reading(float weight_g);

/**
 * @brief Pide vaciar la caja (retirar todos los utiles) y confirmar con el
 * botón táctil, con vigilancia en segundo plano igual que
 * ui_show_wait_weight_used(): no hace falta esperar a que el peso se
 * estabilice para poder pulsar Confirmar, y se puede pulsar tantas veces
 * como haga falta hasta que la caja este realmente vacia.
 */
void ui_show_wait_tare(void);

/**
 * @brief Actualiza solo el texto de la última lectura de peso mostrada en
 * la pantalla de ui_show_wait_tare(), sin tocar los botones.
 */
void ui_update_wait_tare_reading(float weight_g);

/** Peso usados > peso total: no se ha guardado nada, se puede reintentar
 *  la segunda pesada. */
void ui_show_inconsistent_weight(void);

/** Resultado guardado correctamente en inventario.csv. */
void ui_show_result_ok(const char *codigo, int unidades_nuevas, int unidades_usadas);

/** Error fatal de arranque (SD o datos maestros): no hay botones, pantalla fija. */
void ui_show_fatal_error(const char *msg);

/**
 * @brief Teclado numérico en pantalla (disposición tipo numpad) para dar de
 * alta un material nuevo: últimos dígitos del código, unidades totales,
 * calibre y cabeza.
 *
 * @param titulo          Texto de la pregunta (p.ej. "Ultimos digitos del codigo").
 * @param permitir_decimal Si true, la tecla "." está activa (para calibre/cabeza).
 * @param max_len         Longitud máxima de caracteres admitidos.
 */
void ui_show_keypad(const char *titulo, bool permitir_decimal, int max_len);

/**
 * @brief Muestra el peso recién detectado (estable) y pide confirmarlo o
 * repetir la pesada. Se usa en el paso de peso total del alta de un
 * material nuevo, donde sí interesa poder descartar una lectura mal tomada
 * antes de seguir.
 *
 * @param titulo Texto de contexto (p.ej. "Peso total (material nuevo)").
 * @param peso_g Valor detectado, en gramos.
 */
void ui_show_confirm_weight(const char *titulo, float peso_g);

/**
 * @brief Pantalla del modo USB Mass Storage activo. Sin más salida que el
 * botón "Reiniciar" (visible en esta pantalla, además de en reposo).
 */
void ui_show_usb_mode(void);

#ifdef __cplusplus
}
#endif
