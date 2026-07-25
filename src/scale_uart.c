#include "scale_uart.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "bsp_err_check.h"

static const char *TAG = "scale_uart";

#define SCALE_UART_PORT UART_NUM_1
#define SCALE_PIN_TX GPIO_NUM_7
#define SCALE_PIN_RX GPIO_NUM_6
#define SCALE_UART_RX_BUF_SIZE 256

esp_err_t scale_uart_init(int baud_rate, uart_word_length_t data_bits, uart_parity_t parity, uart_stop_bits_t stop_bits)
{
    const uart_config_t cfg = {
        .baud_rate = baud_rate,
        .data_bits = data_bits,
        .parity = parity,
        .stop_bits = stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    BSP_ERROR_CHECK_RETURN_ERR(uart_driver_install(SCALE_UART_PORT, SCALE_UART_RX_BUF_SIZE, 0, 0, NULL, 0));
    BSP_ERROR_CHECK_RETURN_ERR(uart_param_config(SCALE_UART_PORT, &cfg));
    BSP_ERROR_CHECK_RETURN_ERR(uart_set_pin(SCALE_UART_PORT, SCALE_PIN_TX, SCALE_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART1 báscula lista a %d baudios (TX=%d RX=%d)", baud_rate, SCALE_PIN_TX, SCALE_PIN_RX);
    return ESP_OK;
}

/* Buffer de acumulacion entre llamadas: la bascula transmite en continuo y
 * una lectura por ventana de tiempo fija puede caer a mitad de una trama,
 * dando un numero truncado/espurio. Acumulando hasta el siguiente '\n' nos
 * aseguramos de que cada trama devuelta a scale_protocol_parse() esta
 * siempre completa. */
#define SCALE_FRAME_BUF_SIZE 64
static uint8_t s_frame_buf[SCALE_FRAME_BUF_SIZE];
static size_t s_frame_len = 0;

void scale_uart_flush(void)
{
    uart_flush_input(SCALE_UART_PORT);
    s_frame_len = 0;
}

size_t scale_uart_read_frame(uint8_t *out, size_t out_max, uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (1) {
        for (size_t i = 0; i < s_frame_len; i++) {
            if (s_frame_buf[i] != '\n') {
                continue;
            }
            size_t frame_len = i;
            if (frame_len > 0 && s_frame_buf[frame_len - 1] == '\r') {
                frame_len--;
            }
            size_t copy_len = (frame_len < out_max) ? frame_len : out_max;
            memcpy(out, s_frame_buf, copy_len);

            size_t remaining = s_frame_len - (i + 1);
            memmove(s_frame_buf, &s_frame_buf[i + 1], remaining);
            s_frame_len = remaining;
            return copy_len;
        }

        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            return 0;
        }

        if (s_frame_len >= sizeof(s_frame_buf)) {
            /* Trama sin salto de linea que ha llenado el buffer: se
             * descarta para no quedarse atascado con basura. */
            s_frame_len = 0;
        }

        int n = uart_read_bytes(SCALE_UART_PORT, &s_frame_buf[s_frame_len],
                                 sizeof(s_frame_buf) - s_frame_len, deadline - now);
        if (n > 0) {
            s_frame_len += (size_t)n;
        }
    }
}

bool scale_protocol_parse(const uint8_t *raw, size_t len, float *out_weight_g)
{
    size_t i = 0;
    while (i < len) {
        char c = (char)raw[i];
        if (c == '+' || c == '-' || (c >= '0' && c <= '9')) {
            char numbuf[16];
            size_t n = 0;
            size_t j = i;
            bool has_digit = false;

            if (raw[j] == '+' || raw[j] == '-') {
                numbuf[n++] = (char)raw[j++];
            }
            while (j < len && n < sizeof(numbuf) - 1 &&
                   ((raw[j] >= '0' && raw[j] <= '9') || raw[j] == '.')) {
                if (raw[j] >= '0' && raw[j] <= '9') {
                    has_digit = true;
                }
                numbuf[n++] = (char)raw[j++];
            }
            numbuf[n] = '\0';

            if (has_digit) {
                *out_weight_g = strtof(numbuf, NULL);
                return true;
            }
            i = (j > i) ? j : (i + 1);
        } else {
            i++;
        }
    }
    return false;
}
