/*
 * Máquina de estados de la app de inventario. Única tarea (junto con los
 * callbacks de botón LVGL, que solo publican eventos) que invoca las
 * funciones ui_show_*() y por tanto la única que toma bsp_display_lock()
 * desde fuera de la propia tarea LVGL.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arranca la tarea de la máquina de estados.
 *
 * Requiere que app_events_init(), csv_master_load(), csv_inventory_init()
 * y ui_screens_init() ya se hayan llamado.
 */
void app_fsm_start(void);

#ifdef __cplusplus
}
#endif
