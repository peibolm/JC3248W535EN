/*
 * Driver UART1 para la báscula RS232 (a través de un módulo conversor
 * RS232<->TTL externo, MAX3232). Protocolo confirmado con la báscula real:
 * 9600 baudios 8N1, trama ASCII de 14 caracteres en modo de salida continua:
 * signo, espacio, 7 caracteres de digitos/punto decimal, 3 de unidad,
 * "End" y retorno de carro. scale_protocol_parse() queda aislado por si
 * hiciera falta ajustarlo (p.ej. otro modelo de báscula en el futuro).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t scale_uart_init(int baud_rate, uart_word_length_t data_bits, uart_parity_t parity, uart_stop_bits_t stop_bits);

/**
 * @brief Descarta cualquier byte pendiente (buffer hardware de la UART y el
 * de acumulacion de tramas). Imprescindible antes de arrancar una pesada
 * nueva: la bascula transmite en continuo aunque nadie la este leyendo, y
 * sin este flush las primeras muestras podrian ser tramas viejas
 * acumuladas durante el tiempo en que la tarea estuvo inactiva (p.ej.
 * mientras el operario tecleaba en un teclado numerico).
 */
void scale_uart_flush(void);

/**
 * @brief Lee UNA trama completa de la báscula (delimitada por '\n'),
 * acumulando bytes entre llamadas si hace falta. Bloquea como máximo
 * timeout_ms en total; si no se completa ninguna trama en ese tiempo,
 * devuelve 0 sin perder los bytes ya acumulados (se completarán en la
 * siguiente llamada).
 * @return longitud de la trama copiada a out (sin el '\n'/'\r'), o 0 si no
 * llegó ninguna trama completa dentro de timeout_ms.
 */
size_t scale_uart_read_frame(uint8_t *out, size_t out_max, uint32_t timeout_ms);

/**
 * @brief Intenta extraer un peso en gramos de una trama cruda de la báscula.
 *
 * Busca el primer número (con signo y decimales opcionales) en la trama y
 * lo interpreta como gramos; se detiene al llegar a las letras de unidad,
 * por lo que no necesita conocer la posición exacta de cada campo.
 *
 * @return true si se pudo extraer un peso válido.
 */
bool scale_protocol_parse(const uint8_t *raw, size_t len, float *out_weight_g);

#ifdef __cplusplus
}
#endif
