/*
 * Pantallas LVGL de la app de inventario, sustituyen a lv_demo_widgets().
 * Solo app_fsm (y los propios callbacks de botón, que ya corren dentro de
 * la tarea LVGL) deben llamar a estas funciones.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Crea los widgets reutilizables sobre la pantalla activa de disp. */
void ui_screens_init(lv_disp_t *disp);

#define UI_RECENT_DESC_MAX_LEN 64
#define UI_RECENT_MAX 5

typedef struct {
    char descripcion[UI_RECENT_DESC_MAX_LEN];
    int unidades_nuevas;
    int unidades_usadas;
} ui_recent_item_t;

/**
 * @brief Pantalla de espera: "Acerque un articulo al lector", con una
 * tabla de los ultimos articulos registrados (el mas nuevo arriba).
 *
 * @param items Array de hasta UI_RECENT_MAX entradas, la mas reciente en
 * el indice 0. Puede ser NULL si count es 0.
 * @param count Numero de entradas validas en items (0..UI_RECENT_MAX).
 */
void ui_show_idle(const ui_recent_item_t *items, size_t count);

/** Pantalla de error genérica (código no encontrado, duplicado...) con
 *  botón para volver a esperar tag. */
void ui_show_error(const char *msg);

/**
 * @brief Articulo con codigo ya registrado en inventario.csv: ofrece
 * "Aceptar" (volver sin hacer nada) o "Sobrescribir" (repetir el proceso y
 * reemplazar la fila anterior de ese codigo en el inventario).
 */
void ui_show_duplicate_warning(const char *descripcion);

/**
 * @brief Al dar de alta un tag nuevo, el código tecleado ya tiene OTRO tag
 * vinculado en datos_maestros.csv (posible tag físico perdido/sustituido,
 * o error de tecleo). "Sobrescribir" re-vincula el tag que se está
 * registrando a ese código; "Cancelar" vuelve a reposo sin tocar nada.
 */
void ui_show_tag_relink_warning(const char *codigo, const char *descripcion);

/**
 * @brief Al dar de alta un tag nuevo, el código tecleado ya existe en
 * datos_maestros.csv: se muestra la descripción a toda pantalla, ANTES de
 * pedir nada más (unidades, tara...), para poder validar de un vistazo que
 * el código corresponde de verdad al material que se está contando -
 * evita arrastrar un error de tecleo o una caja mal etiquetada varios
 * pasos sin darse cuenta. "Confirmar" continúa; "Revisar código" vuelve a
 * teclearlo; "Cancelar" vuelve a reposo.
 */
void ui_show_confirm_articulo(const char *codigo, const char *descripcion);

/**
 * @brief Aviso de escritura larga en la tarjeta (reescritura completa de
 * inventario.csv al sobrescribir un código ya registrado), para que no
 * parezca que la pantalla se ha colgado.
 *
 * A propósito NO tiene ningún botón: mientras se escribe, la máquina de
 * estados está bloqueada y cualquier toque quedaría encolado para
 * ejecutarse al terminar, disparando una acción que el operario ya no
 * espera.
 *
 * Quien la use debe ceder el paso (~100 ms) antes de empezar a escribir:
 * la tarea de dibujo de LVGL tiene menos prioridad que la máquina de
 * estados, así que sin esa pausa el aviso puede no llegar a pintarse hasta
 * que la escritura ya ha terminado.
 */
void ui_show_saving(void);

/** Muestra la descripción del artículo y pide colocarlo en la báscula. */
void ui_show_description_and_wait_weight(const char *descripcion);

/**
 * @brief Pide retirar los útiles nuevos. LED de estable/inestable en la
 * esquina superior izquierda; abajo, tabla con peso total/unidades
 * totales (fijos) y nuevas/usadas (en vivo, números grandes) para que se
 * vea de un vistazo que el inventario cuadra (nuevas+usadas=totales).
 * El botón Confirmar está disponible desde el primer momento; no hace
 * falta esperar a que el peso se estabilice para poder pulsarlo. Incluye
 * también "Repetir pesada total" por si la pesada de la caja completa no
 * fue correcta.
 */
void ui_show_wait_weight_used(const char *descripcion, int unidades_totales, float peso_total_g);

/**
 * @brief Actualiza las unidades nuevas retiradas y usadas restantes (en
 * vivo, en grande, con un decimal) y el indicador de estable/inestable
 * (verde/rojo) de la pantalla de ui_show_wait_weight_used(). Se llama en
 * cada muestra de la báscula, sea o no estable, para dar feedback continuo.
 *
 * El decimal es a propósito: deja ver de un vistazo cómo de cerca está la
 * cuenta de un número entero, es decir, cómo de fiable es el
 * peso_unitario guardado para este artículo. Si @p peso_unitario_sospechoso
 * es true (el desvío supera la tolerancia), el número se resalta para
 * avisar de que puede haber un error de calibración o algo raro en la caja.
 */
void ui_update_wait_weight_live(float unidades_nuevas, float unidades_usadas, bool stable,
                                 bool peso_unitario_sospechoso);

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

/**
 * @brief Confirmacion de que datos_maestros.csv se ha actualizado (boton
 * "Actualizar datos" desde ui_show_wait_weight_used()). El recuento que
 * estuviera en curso se descarta a proposito, sin tocar inventario.csv.
 */
void ui_show_result_master_updated(const char *codigo, const char *descripcion, float tara_caja, float peso_unitario);

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
