#include "sd_provision.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"

static const char *TAG = "sd_provision";

esp_err_t sd_provision_ensure_master_header(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        ESP_LOGI(TAG, "%s ya existe, no se toca", path);
        return ESP_OK;
    }

    errno = 0;
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo crear %s (errno=%d: %s)", path, errno, strerror(errno));
        return ESP_FAIL;
    }
    fputs("\xEF\xBB\xBF", f); /* BOM UTF-8: sin el, Excel abre el CSV como ANSI */
    fputs("uid_nfc;codigo;descripcion;tara_caja;peso_unitario\n", f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    ESP_LOGI(TAG, "Creado %s vacio (solo cabecera)", path);
    return ESP_OK;
}
