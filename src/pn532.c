#include "pn532.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp_err_check.h"

static const char *TAG = "pn532";

/* Dirección I2C de 7 bits del PN532 (0x48 en notación de 8 bits con R/W). */
#define PN532_I2C_ADDRESS 0x24

#define PN532_CMD_GET_FIRMWARE_VERSION 0x02
#define PN532_CMD_SAM_CONFIGURATION    0x14
#define PN532_CMD_RF_CONFIGURATION     0x32
#define PN532_CMD_IN_LIST_PASSIVE_TARGET 0x4A

#define PN532_FRAME_MAX 32
#define PN532_I2C_TIMEOUT_MS 1000
#define PN532_POLL_INTERVAL_MS 10

static i2c_port_t s_port = I2C_NUM_1;

static esp_err_t i2c_write_raw(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PN532_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(s_port, cmd, pdMS_TO_TICKS(PN532_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* Lee 1 (status) + len bytes en una única transacción. El PN532 antepone
 * siempre un byte de estado en I2C: bit0=1 significa "respuesta lista". */
static esp_err_t i2c_read_status_and_frame(uint8_t *frame_out, size_t frame_len)
{
    uint8_t buf[1 + PN532_FRAME_MAX];
    if (frame_len > PN532_FRAME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PN532_I2C_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, 1 + frame_len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(s_port, cmd, pdMS_TO_TICKS(PN532_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((buf[0] & 0x01) == 0) {
        return ESP_ERR_NOT_FINISHED; /* todavía no hay respuesta */
    }

    memcpy(frame_out, &buf[1], frame_len);
    return ESP_OK;
}

static esp_err_t pn532_write_command(const uint8_t *params, uint8_t params_len)
{
    uint8_t frame[PN532_FRAME_MAX];
    uint8_t idx = 0;

    if ((size_t)(params_len + 9) > sizeof(frame)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tfi = 0xD4; /* host -> PN532 */
    uint8_t len = params_len + 1; /* +TFI */
    uint8_t lcs = (uint8_t)(~len + 1);

    frame[idx++] = 0x00; /* preamble */
    frame[idx++] = 0x00;
    frame[idx++] = 0xFF; /* start code */
    frame[idx++] = len;
    frame[idx++] = lcs;
    frame[idx++] = tfi;

    uint8_t sum = tfi;
    for (uint8_t i = 0; i < params_len; i++) {
        frame[idx++] = params[i];
        sum = (uint8_t)(sum + params[i]);
    }
    frame[idx++] = (uint8_t)(~sum + 1); /* DCS */
    frame[idx++] = 0x00;                /* postamble */

    return i2c_write_raw(frame, idx);
}

static esp_err_t pn532_wait_ack(uint32_t timeout_ms)
{
    static const uint8_t expected_ack[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    uint8_t frame[6];
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        esp_err_t ret = i2c_read_status_and_frame(frame, sizeof(frame));
        if (ret == ESP_OK) {
            if (memcmp(frame, expected_ack, sizeof(frame)) == 0) {
                return ESP_OK;
            }
            ESP_LOGW(TAG, "ACK inesperado del PN532");
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (ret != ESP_ERR_NOT_FINISHED) {
            return ret; /* fallo de bus I2C */
        }
        vTaskDelay(pdMS_TO_TICKS(PN532_POLL_INTERVAL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

/* Extrae los bytes de datos (PD0..PDn, empezando por el código de respuesta)
 * de una trama normal { 00 00 FF LEN LCS TFI PD0..PDn DCS 00 }. */
static esp_err_t parse_response_frame(const uint8_t *frame, uint8_t *out_data, uint8_t max_out, uint8_t *out_len)
{
    if (frame[0] != 0x00 || frame[1] != 0x00 || frame[2] != 0xFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t len = frame[3];
    uint8_t lcs = frame[4];
    if ((uint8_t)(len + lcs) != 0) {
        return ESP_ERR_INVALID_CRC;
    }
    if (len == 0 || len > (PN532_FRAME_MAX - 6)) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t tfi = frame[5];
    if (tfi != 0xD5) { /* PN532 -> host */
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t data_len = len - 1; /* sin contar TFI */
    if (data_len > max_out) {
        data_len = max_out;
    }
    memcpy(out_data, &frame[6], data_len);
    *out_len = data_len;
    return ESP_OK;
}

static esp_err_t pn532_read_response(uint8_t *out_data, uint8_t max_out, uint8_t *out_len, uint32_t timeout_ms)
{
    uint8_t frame[PN532_FRAME_MAX];
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        esp_err_t ret = i2c_read_status_and_frame(frame, sizeof(frame));
        if (ret == ESP_OK) {
            return parse_response_frame(frame, out_data, max_out, out_len);
        }
        if (ret != ESP_ERR_NOT_FINISHED) {
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(PN532_POLL_INTERVAL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

/* El PN532 puede no responder al primer comando tras el arranque (necesita
 * "despertar" en I2C); se reintenta unas pocas veces antes de rendirse. */
static esp_err_t pn532_write_command_retry(const uint8_t *params, uint8_t params_len)
{
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        ret = pn532_write_command(params, params_len);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Fallo al escribir comando, intento %d/3 (%s)", attempt, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ret;
}

static esp_err_t pn532_command(const uint8_t *params, uint8_t params_len,
                                uint8_t *resp_data, uint8_t resp_max, uint8_t *resp_len,
                                uint32_t timeout_ms)
{
    esp_err_t ret = pn532_write_command_retry(params, params_len);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = pn532_wait_ack(timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }
    return pn532_read_response(resp_data, resp_max, resp_len, timeout_ms);
}

esp_err_t pn532_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl)
{
    s_port = port;

    const i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 50000, /* bajado de 100kHz: bus con pull-ups marginales */
    };
    BSP_ERROR_CHECK_RETURN_ERR(i2c_param_config(port, &conf));
    BSP_ERROR_CHECK_RETURN_ERR(i2c_driver_install(port, conf.mode, 0, 0, 0));

    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t pn532_get_firmware_version(uint32_t *out_version)
{
    const uint8_t params[] = {PN532_CMD_GET_FIRMWARE_VERSION};
    uint8_t resp[8];
    uint8_t resp_len = 0;

    esp_err_t ret = pn532_command(params, sizeof(params), resp, sizeof(resp), &resp_len, PN532_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    if (resp_len < 5 || resp[0] != (PN532_CMD_GET_FIRMWARE_VERSION + 1)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (out_version) {
        *out_version = ((uint32_t)resp[1] << 24) | ((uint32_t)resp[2] << 16) |
                       ((uint32_t)resp[3] << 8) | resp[4];
    }
    return ESP_OK;
}

esp_err_t pn532_sam_config(void)
{
    /* modo normal, timeout interno 20*50ms, sin uso de pin IRQ */
    const uint8_t params[] = {PN532_CMD_SAM_CONFIGURATION, 0x01, 0x14, 0x00};
    uint8_t resp[4];
    uint8_t resp_len = 0;

    esp_err_t ret = pn532_command(params, sizeof(params), resp, sizeof(resp), &resp_len, PN532_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    if (resp_len < 1 || resp[0] != (PN532_CMD_SAM_CONFIGURATION + 1)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t pn532_set_passive_activation_retries(uint8_t retries)
{
    /* CfgItem 0x05 (MaxRetries): MxRtyATR, MxRtyPSL, MxRtyPassiveActivation.
     * De fabrica MxRtyPassiveActivation=0xFF (reintentos casi indefinidos
     * dentro del propio chip), por lo que sin esto InListPassiveTarget puede
     * tardar mas que el timeout del host cuando no hay tarjeta en el campo. */
    const uint8_t params[] = {PN532_CMD_RF_CONFIGURATION, 0x05, 0xFF, 0x01, retries};
    uint8_t resp[2];
    uint8_t resp_len = 0;

    esp_err_t ret = pn532_command(params, sizeof(params), resp, sizeof(resp), &resp_len, PN532_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    if (resp_len < 1 || resp[0] != (PN532_CMD_RF_CONFIGURATION + 1)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t pn532_read_passive_target_uid(uint8_t *uid, uint8_t *uid_len, uint32_t timeout_ms)
{
    /* MaxTg=1 tarjeta, BrTy=0x00 -> ISO14443A 106 kbps (Mifare/NTAG) */
    const uint8_t params[] = {PN532_CMD_IN_LIST_PASSIVE_TARGET, 0x01, 0x00};
    uint8_t resp[24];
    uint8_t resp_len = 0;

    esp_err_t ret = pn532_command(params, sizeof(params), resp, sizeof(resp), &resp_len, timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }
    if (resp_len < 2 || resp[0] != (PN532_CMD_IN_LIST_PASSIVE_TARGET + 1)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t nb_tg = resp[1];
    if (nb_tg == 0) {
        return ESP_ERR_NOT_FOUND; /* ninguna tarjeta en el campo, es el caso normal en polling */
    }
    if (resp_len < 8) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t nfcid_len = resp[6];
    if (nfcid_len == 0 || nfcid_len > PN532_MAX_UID_LEN || (uint8_t)(7 + nfcid_len) > resp_len) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    memcpy(uid, &resp[7], nfcid_len);
    *uid_len = nfcid_len;
    return ESP_OK;
}
