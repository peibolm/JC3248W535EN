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

#include <stdbool.h>
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

/**
 * @brief Marca en NVS que el proximo arranque debe entrar DIRECTO en modo
 * USB (sin montar la SD para la app ni levantar NFC/bascula/FSM) y reinicia
 * el dispositivo ya mismo. No vuelve: la funcion no retorna.
 *
 * Por que reiniciar en vez de cambiar en caliente: el ESP32-S3 solo tiene un
 * PHY USB fisico, compartido entre la consola USB-Serial/JTAG (activa todo
 * el rato mientras la app corre) y el modo TinyUSB MSC. Cambiar de uno a
 * otro con la app en marcha - NFC, bascula y LVGL compitiendo por CPU/bus -
 * es una condicion de carrera: unas veces el host USB del PC llega a
 * enganchar bien y otras no, dependiendo de en que anden las demas tareas
 * en ese instante. Reiniciando, el modo USB arranca en un entorno limpio y
 * sin competencia, con el mismo resultado siempre.
 */
void usb_msc_request_boot_and_restart(void);

/**
 * @brief Comprueba si este arranque viene de
 * usb_msc_request_boot_and_restart(). Si es asi, limpia la marca en NVS (para
 * que el SIGUIENTE reinicio sea uno normal) y devuelve true.
 *
 * Llamar a esto LO PRIMERO en el arranque, antes de montar la SD para la app
 * o de levantar NFC/bascula/FSM - esas tareas son precisamente las que hay
 * que evitar arrancar en modo USB.
 */
bool usb_msc_should_boot_into_usb_mode(void);

#ifdef __cplusplus
}
#endif
