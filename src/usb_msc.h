/*
 * Modo "USB Mass Storage": expone la tarjeta microSD como un disco USB
 * normal para el PC. Es un modo aparte, no compatible con el funcionamiento
 * normal de la app (el ESP32 y el PC no pueden usar la SD a la vez) — una
 * vez activado, la única forma de volver es reiniciar el dispositivo.
 *
 * Aviso importante: mientras este modo está activo, la consola/log por USB
 * (USB-Serial/JTAG) deja de estar disponible por ese mismo cable, ya que el
 * ESP32-S3 solo puede usar un periferico USB a la vez sobre el mismo PHY
 * fisico. El flasheo de firmware no se ve afectado (usa el modo de arranque
 * de la ROM, independiente de esto).
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Desmonta la SD del uso normal de la app y la expone por USB como
 * almacenamiento masivo. No retorna a modo normal por si sola: hace falta
 * reiniciar el dispositivo (botón "Reiniciar" en pantalla).
 */
esp_err_t usb_msc_start(void);

#ifdef __cplusplus
}
#endif
