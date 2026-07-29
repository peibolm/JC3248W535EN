#include "csv_inventory.h"
#include "csv_master.h" /* MASTER_CODIGO_MAX_LEN */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char *TAG = "csv_inventory";

#define INVENTORY_PATH_MAX_LEN 128
#define INVENTORY_FIELD_SEP ';' /* mismo separador que datos_maestros.csv */
#define INVENTORY_HEADER "codigo;unidades_nuevas;unidades_usadas;hora_desde_arranque\n"

/* No hay RTC ni hora de red en este dispositivo: se usa el tiempo
 * transcurrido desde el arranque (HH:MM:SS) solo para poder medir cuanto
 * se tarda en hacer el inventario, no como hora real. */
static void format_uptime_hms(char *out, size_t out_size)
{
    int64_t total_s = esp_timer_get_time() / 1000000LL;
    int hh = (int)(total_s / 3600);
    int mm = (int)((total_s % 3600) / 60);
    int ss = (int)(total_s % 60);
    snprintf(out, out_size, "%02d:%02d:%02d", hh, mm, ss);
}

typedef char codigo_str_t[MASTER_CODIGO_MAX_LEN];

static char s_path[INVENTORY_PATH_MAX_LEN];
static codigo_str_t *s_codes = NULL;
static size_t s_count = 0;
static size_t s_capacity = 0;

/* Ultimas entradas tocadas (la mas nueva siempre en el indice 0), para la
 * tabla resumen de la pantalla de reposo. Se repuebla tambien al cargar el
 * fichero existente, para que sobreviva a un reinicio. */
static csv_inventory_recent_t s_recent[CSV_INVENTORY_RECENT_MAX];
static size_t s_recent_count = 0;

static void push_recent(const char *codigo, int unidades_nuevas, int unidades_usadas)
{
    size_t n = (s_recent_count < CSV_INVENTORY_RECENT_MAX) ? s_recent_count : CSV_INVENTORY_RECENT_MAX - 1;
    for (size_t i = n; i > 0; i--) {
        s_recent[i] = s_recent[i - 1];
    }
    strlcpy(s_recent[0].codigo, codigo, sizeof(s_recent[0].codigo));
    s_recent[0].unidades_nuevas = unidades_nuevas;
    s_recent[0].unidades_usadas = unidades_usadas;
    if (s_recent_count < CSV_INVENTORY_RECENT_MAX) {
        s_recent_count++;
    }
}

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
    size_t new_capacity = (s_capacity == 0) ? 64 : s_capacity * 2;
    codigo_str_t *new_codes = heap_caps_realloc(s_codes, new_capacity * sizeof(codigo_str_t), MALLOC_CAP_SPIRAM);
    if (!new_codes) {
        ESP_LOGE(TAG, "Sin memoria para %u codigos de inventario", (unsigned)new_capacity);
        return false;
    }
    s_codes = new_codes;
    s_capacity = new_capacity;
    return true;
}

static void remember_codigo(const char *codigo)
{
    if (!ensure_capacity()) {
        return;
    }
    strlcpy(s_codes[s_count], codigo, sizeof(codigo_str_t));
    s_count++;
}

static esp_err_t create_with_header(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo crear %s", path);
        return ESP_FAIL;
    }
    fputs(INVENTORY_HEADER, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return ESP_OK;
}

/* Divide una linea en codigo;unidades_nuevas;unidades_usadas (el 4o campo,
 * hora, es opcional y se ignora aqui). Devuelve false si faltan columnas. */
static bool parse_row(char *line, char **codigo_out, int *nuevas_out, int *usadas_out)
{
    char *codigo = line;
    char *sep1 = strchr(codigo, INVENTORY_FIELD_SEP);
    if (!sep1) {
        return false;
    }
    *sep1 = '\0';

    char *nuevas_str = sep1 + 1;
    char *sep2 = strchr(nuevas_str, INVENTORY_FIELD_SEP);
    if (!sep2) {
        return false;
    }
    *sep2 = '\0';

    char *usadas_str = sep2 + 1;
    char *sep3 = strchr(usadas_str, INVENTORY_FIELD_SEP); /* hora, opcional */
    if (sep3) {
        *sep3 = '\0';
    }

    *codigo_out = codigo;
    *nuevas_out = atoi(nuevas_str);
    *usadas_out = atoi(usadas_str);
    return true;
}

static esp_err_t load_existing_codes(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
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
        char *codigo;
        int nuevas, usadas;
        if (parse_row(line, &codigo, &nuevas, &usadas)) {
            remember_codigo(codigo);
            push_recent(codigo, nuevas, usadas);
        }
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t csv_inventory_init(const char *path)
{
    strlcpy(s_path, path, sizeof(s_path));
    s_count = 0;
    s_recent_count = 0;

    FILE *probe = fopen(path, "r");
    if (!probe) {
        ESP_LOGI(TAG, "%s no existe, se crea con cabecera", path);
        return create_with_header(path);
    }
    fclose(probe);

    esp_err_t ret = load_existing_codes(path);
    ESP_LOGI(TAG, "Cargados %u codigos ya registrados desde %s", (unsigned)s_count, path);
    return ret;
}

bool csv_inventory_has_codigo(const char *codigo)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_codes[i], codigo) == 0) {
            return true;
        }
    }
    return false;
}

esp_err_t csv_inventory_append(const char *codigo, int unidades_nuevas, int unidades_usadas)
{
    char hora[12];
    format_uptime_hms(hora, sizeof(hora));

    FILE *f = fopen(s_path, "a");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo abrir %s para anexar", s_path);
        return ESP_FAIL;
    }

    fprintf(f, "%s%c%d%c%d%c%s\n", codigo, INVENTORY_FIELD_SEP, unidades_nuevas,
            INVENTORY_FIELD_SEP, unidades_usadas, INVENTORY_FIELD_SEP, hora);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    remember_codigo(codigo);
    push_recent(codigo, unidades_nuevas, unidades_usadas);

    ESP_LOGI(TAG, "Guardado en inventario: %s,%d,%d,%s", codigo, unidades_nuevas, unidades_usadas, hora);
    return ESP_OK;
}

esp_err_t csv_inventory_append_or_update(const char *codigo, int unidades_nuevas, int unidades_usadas)
{
    if (!csv_inventory_has_codigo(codigo)) {
        return csv_inventory_append(codigo, unidades_nuevas, unidades_usadas);
    }

    char hora[12];
    format_uptime_hms(hora, sizeof(hora));

    /* Reescribe el fichero entero copiando todas las filas tal cual, salvo
     * la de este codigo, que se sustituye por los valores nuevos. */
    FILE *in = fopen(s_path, "r");
    if (!in) {
        ESP_LOGE(TAG, "No se pudo abrir %s para sobrescribir", s_path);
        return ESP_FAIL;
    }

    char tmp_path[INVENTORY_PATH_MAX_LEN + 4];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", s_path);
    FILE *out = fopen(tmp_path, "w");
    if (!out) {
        ESP_LOGE(TAG, "No se pudo crear %s", tmp_path);
        fclose(in);
        return ESP_FAIL;
    }

    char line[96];
    bool first_line = true;
    bool replaced = false;
    while (fgets(line, sizeof(line), in)) {
        if (first_line) {
            first_line = false;
            fputs(line, out);
            continue;
        }

        char linecopy[96];
        strlcpy(linecopy, line, sizeof(linecopy));
        trim_newline(linecopy);
        char *sep = strchr(linecopy, INVENTORY_FIELD_SEP);
        if (sep) {
            *sep = '\0';
        }

        if (strcasecmp(linecopy, codigo) == 0) {
            fprintf(out, "%s%c%d%c%d%c%s\n", codigo, INVENTORY_FIELD_SEP, unidades_nuevas,
                    INVENTORY_FIELD_SEP, unidades_usadas, INVENTORY_FIELD_SEP, hora);
            replaced = true;
        } else {
            fputs(line, out);
        }
    }
    if (!replaced) {
        fprintf(out, "%s%c%d%c%d%c%s\n", codigo, INVENTORY_FIELD_SEP, unidades_nuevas,
                INVENTORY_FIELD_SEP, unidades_usadas, INVENTORY_FIELD_SEP, hora);
    }

    fflush(out);
    fsync(fileno(out));
    fclose(out);
    fclose(in);

    remove(s_path);
    rename(tmp_path, s_path);

    push_recent(codigo, unidades_nuevas, unidades_usadas);

    ESP_LOGI(TAG, "Inventario actualizado (sobrescrito): %s,%d,%d,%s", codigo, unidades_nuevas, unidades_usadas, hora);
    return ESP_OK;
}

size_t csv_inventory_get_recent(csv_inventory_recent_t *out, size_t max_out)
{
    size_t n = (s_recent_count < max_out) ? s_recent_count : max_out;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_recent[i];
    }
    return n;
}
