#include "csv_master.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "csv_master";

#define MASTER_PATH_MAX_LEN 128

static master_item_t *s_items = NULL;
static size_t s_count = 0;
static size_t s_capacity = 0;
static char s_path[MASTER_PATH_MAX_LEN];
static char s_field_sep = ','; /* detectado en csv_master_load() a partir de la cabecera */

/* Sin este BOM, Excel abre el CSV como ANSI/Windows-1252 en vez de UTF-8 y
 * las tildes/Ø/º de la descripcion se ven como "Ã“"/"Ã˜"/"Âº". */
#define CSV_UTF8_BOM "\xEF\xBB\xBF"

static esp_err_t rewrite_full_file(void);

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
    master_item_t *new_items = heap_caps_realloc(s_items, new_capacity * sizeof(master_item_t), MALLOC_CAP_SPIRAM);
    if (!new_items) {
        ESP_LOGE(TAG, "Sin memoria para %u entradas de datos maestros", (unsigned)new_capacity);
        return false;
    }
    s_items = new_items;
    s_capacity = new_capacity;
    return true;
}

/* Acepta tanto "0.150" (punto decimal) como "0,150" (coma decimal, formato
 * de Excel en España) para los campos numéricos. */
static float parse_decimal(const char *field)
{
    char buf[32];
    strlcpy(buf, field, sizeof(buf));
    char *comma = strchr(buf, ',');
    if (comma) {
        *comma = '.';
    }
    return strtof(buf, NULL);
}

/* Divide una línea en sus 5 campos: uid,codigo,descripcion,tara,peso_unitario.
 * El separador de campo se detecta por línea: si aparece ';' se usa como tal
 * (habitual al exportar CSV desde Excel en España, donde ',' es el separador
 * decimal); si no, se usa ','. */
static bool parse_line(char *line, master_item_t *out)
{
    char sep = strchr(line, ';') ? ';' : ',';

    char *fields[5];
    char *cursor = line;

    for (int i = 0; i < 5; i++) {
        fields[i] = cursor;
        if (i < 4) {
            char *delim = strchr(cursor, sep);
            if (!delim) {
                return false; /* faltan columnas */
            }
            *delim = '\0';
            cursor = delim + 1;
        }
    }

    memset(out, 0, sizeof(*out));
    strlcpy(out->uid_nfc, fields[0], sizeof(out->uid_nfc));
    strlcpy(out->codigo, fields[1], sizeof(out->codigo));
    strlcpy(out->descripcion, fields[2], sizeof(out->descripcion));
    out->tara_caja = parse_decimal(fields[3]);
    out->peso_unitario = parse_decimal(fields[4]);

    /* uid_nfc puede venir vacio (material precatalogado, pendiente de
     * vincular a un tag fisico con csv_master_link_uid()). */
    return (out->codigo[0] != '\0') && (out->peso_unitario > 0.0f);
}

esp_err_t csv_master_load(const char *path)
{
    errno = 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo abrir %s (errno=%d: %s)", path, errno, strerror(errno));
        return ESP_ERR_NOT_FOUND;
    }

    strlcpy(s_path, path, sizeof(s_path));
    s_count = 0;

    /* Si el fichero ya trae BOM (lo escribimos nosotros al reescribirlo, o
     * viene de Excel guardado como "CSV UTF-8"), nos lo saltamos para que no
     * se cuele en la cabecera; si no lo trae, se anade mas abajo al detectar
     * que faltaba, para que Excel muestre bien tildes/Ø/º. */
    bool had_bom = (fgetc(f) == 0xEF && fgetc(f) == 0xBB && fgetc(f) == 0xBF);
    if (!had_bom) {
        rewind(f);
    }

    char line[192];
    bool first_line = true;
    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (first_line) {
            first_line = false;
            /* El separador de campo de todo el fichero se fija a partir de
             * la cabecera (comas o puntos y coma tipo Excel-ES). */
            s_field_sep = strchr(line, ';') ? ';' : ',';
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
            ESP_LOGW(TAG, "Linea invalida ignorada en datos_maestros.csv: %s", line);
        }
    }
    fclose(f);

    ESP_LOGI(TAG, "Cargadas %u entradas de %s", (unsigned)s_count, path);

    /* 0 filas es un estado valido (solo cabecera): los articulos se pueden
     * ir anadiendo despues desde la app via el alta de material nuevo. */
    if (!had_bom) {
        ESP_LOGW(TAG, "%s sin BOM UTF-8, reescribiendo para que Excel muestre bien las tildes/Ø/º", path);
        rewrite_full_file();
    }

    return ESP_OK;
}

const master_item_t *csv_master_find_by_uid(const char *uid_nfc)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_items[i].uid_nfc, uid_nfc) == 0) {
            return &s_items[i];
        }
    }
    return NULL;
}

const master_item_t *csv_master_find_by_codigo(const char *codigo)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_items[i].codigo, codigo) == 0) {
            return &s_items[i];
        }
    }
    return NULL;
}

size_t csv_master_count(void)
{
    return s_count;
}

/* Escribe una fila con el mismo separador de campo y estilo decimal que ya
 * usa el fichero (coma decimal si el campo es ';', punto si es ','). */
static void write_row(FILE *f, const master_item_t *item)
{
    char tara_str[16];
    char peso_str[16];
    snprintf(tara_str, sizeof(tara_str), "%.3f", item->tara_caja);
    snprintf(peso_str, sizeof(peso_str), "%.4f", item->peso_unitario);
    if (s_field_sep == ';') {
        char *p;
        if ((p = strchr(tara_str, '.')) != NULL) {
            *p = ',';
        }
        if ((p = strchr(peso_str, '.')) != NULL) {
            *p = ',';
        }
    }
    fprintf(f, "%s%c%s%c%s%c%s%c%s\n",
            item->uid_nfc, s_field_sep,
            item->codigo, s_field_sep,
            item->descripcion, s_field_sep,
            tara_str, s_field_sep,
            peso_str);
}

/* Copia tal cual (best-effort) el fichero actual a datos_maestros.csv.bak
 * antes de tocarlo. Si falla, solo se avisa por log: no debe impedir la
 * escritura principal (y si el fichero aun no existe -primera vez- no hay
 * nada que respaldar). */
static void backup_before_write(void)
{
    char bak_path[MASTER_PATH_MAX_LEN + 4];
    snprintf(bak_path, sizeof(bak_path), "%s.bak", s_path);

    FILE *in = fopen(s_path, "r");
    if (!in) {
        return;
    }
    FILE *out = fopen(bak_path, "w");
    if (!out) {
        ESP_LOGW(TAG, "No se pudo crear la copia de seguridad %s", bak_path);
        fclose(in);
        return;
    }

    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fflush(out);
    fsync(fileno(out));
    fclose(out);
    fclose(in);
}

/* Reescribe el fichero entero desde la tabla en RAM, con BOM UTF-8 al
 * principio (necesario para que Excel muestre bien las tildes/Ø/º de la
 * descripcion). Usado tanto al vincular un UID como al actualizar una
 * entrada existente, y al detectar, al cargar, que el fichero en la SD
 * todavia no tiene el BOM.
 *
 * Antes de escribir se guarda una copia de seguridad (backup_before_write),
 * y la escritura en si va a un fichero .tmp + fsync, sustituyendo el
 * original solo al final (remove + rename) - nunca se trunca el fichero
 * real en sitio. FatFs no permite un rename() atomico que sustituya un
 * destino ya existente (falla si el destino existe), asi que el remove+
 * rename no se puede evitar del todo, pero asi se reduce la ventana de riesgo
 * de "todo el tiempo que tarda en escribirse el fichero entero" a "dos
 * llamadas seguidas" - y con la copia de seguridad, un fallo a medias deja
 * de todas formas un .bak recuperable a mano desde la SD. */
static esp_err_t rewrite_full_file(void)
{
    backup_before_write();

    char tmp_path[MASTER_PATH_MAX_LEN + 4];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", s_path);

    errno = 0;
    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo crear %s (errno=%d: %s)", tmp_path, errno, strerror(errno));
        return ESP_FAIL;
    }
    fputs(CSV_UTF8_BOM, f);
    fprintf(f, "uid_nfc%ccodigo%cdescripcion%ctara_caja%cpeso_unitario\n",
            s_field_sep, s_field_sep, s_field_sep, s_field_sep);
    for (size_t i = 0; i < s_count; i++) {
        write_row(f, &s_items[i]);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    errno = 0;
    remove(s_path);
    if (rename(tmp_path, s_path) != 0) {
        ESP_LOGE(TAG, "No se pudo renombrar %s a %s (errno=%d: %s)", tmp_path, s_path, errno, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t csv_master_append(const master_item_t *item)
{
    if (!ensure_capacity()) {
        return ESP_ERR_NO_MEM;
    }

    errno = 0;
    FILE *f = fopen(s_path, "a");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo abrir %s para anadir entrada maestra (errno=%d: %s)",
                 s_path, errno, strerror(errno));
        return ESP_FAIL;
    }
    write_row(f, item);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    s_items[s_count] = *item;
    s_count++;

    ESP_LOGI(TAG, "Nueva entrada anadida a datos maestros: %s (%s)", item->codigo, item->descripcion);
    return ESP_OK;
}

esp_err_t csv_master_link_uid(const char *codigo, const char *uid_nfc)
{
    size_t idx = 0;
    bool found = false;
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_items[i].codigo, codigo) == 0) {
            idx = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }

    strlcpy(s_items[idx].uid_nfc, uid_nfc, sizeof(s_items[idx].uid_nfc));

    /* Se reescribe el fichero entero desde la tabla en RAM: es la unica
     * forma sencilla de actualizar una fila ya existente (a diferencia de
     * csv_master_append(), que solo anade). Con unos pocos miles de filas
     * como mucho, y siendo una operacion puntual (vincular un tag nuevo a
     * su caja), reescribir el fichero completo es rapido y sin riesgo. */
    esp_err_t ret = rewrite_full_file();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "UID %s vinculado al codigo %s (datos_maestros.csv reescrito)", uid_nfc, codigo);
    return ESP_OK;
}

esp_err_t csv_master_update_by_codigo(const master_item_t *item)
{
    size_t idx = 0;
    bool found = false;
    for (size_t i = 0; i < s_count; i++) {
        if (strcasecmp(s_items[i].codigo, item->codigo) == 0) {
            idx = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }

    s_items[idx] = *item;

    esp_err_t ret = rewrite_full_file();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Datos maestros actualizados para %s (datos_maestros.csv reescrito)", item->codigo);
    return ESP_OK;
}
