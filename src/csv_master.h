/*
 * Carga de datos_maestros.csv (uid_nfc,codigo,descripcion,tara_caja,peso_unitario)
 * a una tabla en RAM/PSRAM para lookup por UID de tag NFC. El campo uid_nfc
 * puede venir vacío (material precatalogado en el CSV, pendiente de que se
 * pegue un tag físico a su caja); csv_master_link_uid() vincula ese tag más
 * tarde sin necesidad de volver a pedir tara/peso_unitario/descripción.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MASTER_UID_MAX_LEN  32
#define MASTER_CODIGO_MAX_LEN 32
#define MASTER_DESC_MAX_LEN  64

typedef struct {
    char uid_nfc[MASTER_UID_MAX_LEN];
    char codigo[MASTER_CODIGO_MAX_LEN];
    char descripcion[MASTER_DESC_MAX_LEN];
    float tara_caja;      /* g */
    float peso_unitario;  /* g */
} master_item_t;

/**
 * @brief Carga el fichero CSV completo (con cabecera) a memoria.
 *
 * Se debe llamar una única vez al arranque, tras montar la microSD.
 *
 * @param path Ruta absoluta al fichero, p.ej. "/sdcard/datos_maestros.csv"
 * @return ESP_OK si se cargó al menos una entrada válida.
 */
esp_err_t csv_master_load(const char *path);

/**
 * @brief Busca una entrada por uid_nfc (comparación exacta, case-insensitive).
 *
 * @return puntero a la entrada (válido mientras csv_master_load no se vuelva a
 *         llamar) o NULL si no se encuentra.
 */
const master_item_t *csv_master_find_by_uid(const char *uid_nfc);

/**
 * @brief Busca una entrada por código de artículo (comparación exacta,
 * case-insensitive). Se usa tanto para evitar generar un código ya
 * existente al dar de alta un material nuevo, como para comprobar si un
 * código tecleado corresponde a un material precatalogado (uid_nfc vacío)
 * pendiente de vincular a un tag.
 *
 * @return puntero a la entrada o NULL si no se encuentra.
 */
const master_item_t *csv_master_find_by_codigo(const char *codigo);

/**
 * @brief Número de entradas cargadas.
 */
size_t csv_master_count(void);

/**
 * @brief Añade una entrada nueva tanto a la tabla en RAM (queda disponible
 * de inmediato para csv_master_find_by_uid/codigo) como al fichero CSV en
 * la SD (usa la misma ruta y el mismo separador de campo detectados al
 * cargar con csv_master_load()).
 */
esp_err_t csv_master_append(const master_item_t *item);

/**
 * @brief Vincula un UID de tag a una entrada ya existente (buscada por
 * código), tanto en RAM como reescribiendo el fichero CSV completo en la SD.
 *
 * @return ESP_OK si se encontró y actualizó el código; ESP_ERR_NOT_FOUND si
 *         no existe ninguna entrada con ese código.
 */
esp_err_t csv_master_link_uid(const char *codigo, const char *uid_nfc);

/**
 * @brief Sustituye descripcion/tara_caja/peso_unitario de una entrada ya
 * existente (buscada por codigo), tanto en RAM como reescribiendo el
 * fichero CSV completo en la SD. El uid_nfc lo decide el llamador (se suele
 * pasar el mismo que ya tenia, sin cambiarlo).
 *
 * Antes de reescribir se guarda una copia de seguridad del fichero tal cual
 * estaba (datos_maestros.csv.bak), y la reescritura en si se hace a un
 * fichero temporal + fsync + reemplazo, no truncando el original en sitio -
 * ver el comentario de rewrite_full_file() en csv_master.c.
 *
 * @return ESP_OK si se encontro y actualizo el codigo; ESP_ERR_NOT_FOUND si
 *         no existe ninguna entrada con ese codigo.
 */
esp_err_t csv_master_update_by_codigo(const master_item_t *item);

#ifdef __cplusplus
}
#endif
