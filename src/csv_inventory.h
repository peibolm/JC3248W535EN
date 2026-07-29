/*
 * Gestión de inventario.csv (codigo;unidades_nuevas;unidades_usadas;hora).
 * Una línea por código de artículo; detecta duplicados en RAM sin releer
 * el fichero en cada lectura de tag.
 *
 * El campo "hora" es tiempo transcurrido desde el arranque del ESP32
 * (HH:MM:SS), no la hora real (el dispositivo no tiene RTC/hora de red);
 * sirve para medir cuanto se tarda en hacer el inventario, no como marca
 * de tiempo absoluta.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "csv_master.h" /* MASTER_CODIGO_MAX_LEN */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Abre (o crea con cabecera) el fichero de inventario y carga los
 * códigos ya registrados a memoria para chequeo rápido de duplicados.
 *
 * @param path Ruta absoluta, p.ej. "/sdcard/inventario.csv"
 */
esp_err_t csv_inventory_init(const char *path);

/**
 * @brief Indica si el código ya tiene una línea registrada.
 */
bool csv_inventory_has_codigo(const char *codigo);

/**
 * @brief Añade una línea nueva al fichero y actualiza la tabla en RAM.
 *
 * No comprueba duplicados internamente: el llamador debe comprobar
 * csv_inventory_has_codigo() antes.
 */
esp_err_t csv_inventory_append(const char *codigo, int unidades_nuevas, int unidades_usadas);

/**
 * @brief Igual que csv_inventory_append(), pero si el codigo ya tiene una
 * fila registrada la sustituye en vez de anadir una duplicada.
 */
esp_err_t csv_inventory_append_or_update(const char *codigo, int unidades_nuevas, int unidades_usadas);

#define CSV_INVENTORY_RECENT_MAX 5

typedef struct {
    char codigo[MASTER_CODIGO_MAX_LEN];
    int unidades_nuevas;
    int unidades_usadas;
} csv_inventory_recent_t;

/**
 * @brief Copia hasta max_out de las entradas mas recientes (la mas nueva
 * en el indice 0), incluyendo las cargadas de la SD al arrancar. Util para
 * mostrar un resumen en la pantalla de reposo.
 *
 * @return numero de entradas copiadas (puede ser 0 si aun no hay ninguna).
 */
size_t csv_inventory_get_recent(csv_inventory_recent_t *out, size_t max_out);

#ifdef __cplusplus
}
#endif
