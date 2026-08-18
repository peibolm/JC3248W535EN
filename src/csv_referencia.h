/*
 * Carga opcional de inventario_referencia.csv (codigo,unidades_nuevas,
 * unidades_usadas): el stock teorico/informatico que deberia haber por
 * codigo, para comparar con lo que se va contando en pantalla.
 *
 * Es un asistente para detectar posibles errores de conteo, no un
 * requisito: la app nunca escribe este fichero, y si no existe o un codigo
 * no aparece en el, el recuento sigue funcionando exactamente igual, solo
 * sin mostrar diferencia para ese caso.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFERENCIA_CODIGO_MAX_LEN 32

typedef struct {
    char codigo[REFERENCIA_CODIGO_MAX_LEN];
    int unidades_nuevas;
    int unidades_usadas;
} referencia_item_t;

/**
 * @brief Carga el fichero a memoria, si existe. Se debe llamar una unica vez
 * al arrancar, tras montar la microSD (no hace falta ningun otro paso
 * previo: a diferencia de datos_maestros.csv, este fichero nunca se
 * reescribe desde la app, asi que no necesita recuperacion de escritura
 * interrumpida ni fichero de cabecera creado por defecto).
 *
 * @return ESP_OK si se cargo al menos una entrada; ESP_ERR_NOT_FOUND si el
 * fichero no existe. Ninguno de los dos casos es un error fatal para la
 * app: sin fichero, la comparacion con el stock teorico simplemente queda
 * desactivada.
 */
esp_err_t csv_referencia_load(const char *path);

/**
 * @brief Busca el stock teorico de un codigo (comparacion exacta,
 * case-insensitive).
 *
 * @return puntero a la entrada, o NULL si no hay fichero cargado o el
 * codigo no figura en el.
 */
const referencia_item_t *csv_referencia_find_by_codigo(const char *codigo);

#ifdef __cplusplus
}
#endif
