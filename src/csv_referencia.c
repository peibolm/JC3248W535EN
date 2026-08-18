#include "csv_referencia.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "csv_referencia";

static referencia_item_t *s_items = NULL;
static size_t s_count = 0;
static size_t s_capacity = 0;

static void trim_newline(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static bool ensure_capacity(void)
{
    if (s_count < s_capacity) {
        return true;
    }
    size_t new_capacity = (s_capacity == 0) ? 32 : s_capacity * 2;
    referencia_item_t *new_items = heap_caps_realloc(s_items, new_capacity * sizeof(referencia_item_t), MALLOC_CAP_SPIRAM);
    if (!new_items) {
        ESP_LOGE(TAG, "Sin memoria para %u entradas de referencia", (unsigned)new_capacity);
        return false;
    }
    s_items = new_items;
    s_capacity = new_capacity;
    return true;
}

/* codigo,unidades_nuevas,unidades_usadas - mismo criterio de separador que
 * datos_maestros.csv/inventario.csv: ';' si aparece en la linea (Excel-ES),
 * ',' si no. */
static bool parse_line(char *line, referencia_item_t *out)
{
    char sep = strchr(line, ';') ? ';' : ',';

    char *fields[3];
    char *cursor = line;
    for (int i = 0; i < 3; i++) {
        fields[i] = cursor;
        if (i < 2) {
            char *delim = strchr(cursor, sep);
            if (!delim) {
                return false; /* faltan columnas */
            }
            *delim = '\0';
            cursor = delim + 1;
        }
    }

    memset(out, 0, sizeof(*out));
    strlcpy(out->codigo, fields[0], sizeof(out->codigo));
    out->unidades_nuevas = (int)strtol(fields[1], NULL, 10);
    out->unidades_usadas = (int)strtol(fields[2], NULL, 10);

    return out->codigo[0] != '\0';
}

esp_err_t csv_referencia_load(const char *path)
{
    s_count = 0;

    errno = 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "%s no encontrado; la comparacion con el stock teorico queda desactivada", path);
        return ESP_ERR_NOT_FOUND;
    }

    /* Igual que datos_maestros.csv/inventario.csv: si el fichero trae BOM
     * UTF-8 (guardado como "CSV UTF-8" desde Excel), se salta para que no
     * se cuele en la cabecera. */
    bool had_bom = (fgetc(f) == 0xEF && fgetc(f) == 0xBB && fgetc(f) == 0xBF);
    if (!had_bom) {
        rewind(f);
    }

    char line[96];
    bool first_line = true;
    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (first_line) {
            first_line = false;
            continue; /* cabecera */
        }
        if (line[0] == '\0') {
            continue;
        }
        if (!ensure_capacity()) {
            break;
        }
        if (parse_line(line, &s_items[s_count])) {
            s_count++;
        } else {
            ESP_LOGW(TAG, "Linea invalida ignorada en inventario_referencia.csv: %s", line);
        }
    }
    fclose(f);

    ESP_LOGI(TAG, "Cargadas %u entradas de %s", (unsigned)s_count, path);
    return ESP_OK;
}

const referencia_item_t *csv_referencia_find_by_codigo(const char *codigo)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_items[i].codigo, codigo) == 0) {
            return &s_items[i];
        }
    }
    return NULL;
}
