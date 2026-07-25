/*
 * Ayuda para primer arranque: si datos_maestros.csv no existe todavía en
 * la tarjeta, el propio ESP32 lo crea con solo la cabecera (mismo
 * separador ';' y BOM UTF-8 que usa csv_master.c al reescribirlo), listo
 * para ir dando de alta artículos desde la app. Nunca sobreescribe un
 * fichero ya existente.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sd_provision_ensure_master_header(const char *path);

#ifdef __cplusplus
}
#endif
