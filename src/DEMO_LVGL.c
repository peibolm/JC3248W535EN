
// #include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include <esp_log.h>   // Add this line to include the header file that declares ESP_LOGI
#include <esp_flash.h> // Add this line to include the header file that declares esp_flash_t
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

#include "storage_sd.h"
#include "sd_provision.h"
#include "csv_master.h"
#include "csv_inventory.h"
#include "app_events.h"
#include "ui_screens.h"
#include "app_fsm.h"
#include "nfc_task.h"
#include "scale_task.h"
#include "usb_msc.h"

#define DATOS_MAESTROS_PATH "/sdcard/datos_maestros.csv"
#define INVENTARIO_PATH     "/sdcard/inventario.csv"

static const char *TAG = "DEMO_LVGL";

#define BUILD (String(__DATE__) + " - " + String(__TIME__)).c_str()

#define logSection(section) \
  ESP_LOGI(TAG, "\n\n************* %s **************\n", section);

/**
 * @brief LVGL porting example
 * Set the rotation degree:
 *      - 0: 0 degree
 *      - 90: 90 degree
 *      - 180: 180 degree
 *      - 270: 270 degree
 *
 */
#define LVGL_PORT_ROTATION_DEGREE (90)

void setup();

#if !CONFIG_AUTOSTART_ARDUINO
void app_main()
{
  // initialize arduino library before we start the tasks
  // initArduino();

  setup();
}
#endif
void setup()
{
  //  String title = "LVGL porting example";

  // Serial.begin(115200);
  logSection("LVGL porting example start");
  esp_chip_info_t chip_info;
  uint32_t flash_size;
  esp_chip_info(&chip_info);
  ESP_LOGI(TAG, "This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

  unsigned major_rev = chip_info.revision / 100;
  unsigned minor_rev = chip_info.revision % 100;
  ESP_LOGI(TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);
  if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
  {
    ESP_LOGI(TAG, "Get flash size failed");
    return;
  }

  ESP_LOGI(TAG, "%" PRIu32 "MB %s flash", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

  ESP_LOGI(TAG, "Minimum free heap size: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());
  size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(TAG, "Free PSRAM: %d bytes", freePsram);
  logSection("Initialize panel device");
  // ESP_LOGI(TAG, "Initialize panel device");
  bsp_display_cfg_t cfg = {
      .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
      .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
#if LVGL_PORT_ROTATION_DEGREE == 90
      .rotate = LV_DISP_ROT_90,
#elif LVGL_PORT_ROTATION_DEGREE == 270
      .rotate = LV_DISP_ROT_270,
#elif LVGL_PORT_ROTATION_DEGREE == 180
      .rotate = LV_DISP_ROT_180,
#elif LVGL_PORT_ROTATION_DEGREE == 0
      .rotate = LV_DISP_ROT_NONE,
#endif
  };

  lv_disp_t *disp = bsp_display_start_with_config(&cfg);
  bsp_display_backlight_on();

  logSection("Create UI");
  ui_screens_init(disp);

  /* Si el ultimo reinicio vino del boton "Modo USB", entra DIRECTO en modo
   * USB y ya esta: nada de montar la SD para la app, ni NFC, ni bascula,
   * ni FSM. Cuanto menos compita por CPU/bus durante el enganche USB con
   * el PC, mas fiable (ver usb_msc_request_boot_and_restart()). */
  if (usb_msc_should_boot_into_usb_mode()) {
    logSection("Boot directo a modo USB");
    if (usb_msc_start() != ESP_OK) {
      ui_show_fatal_error("Fallo al activar el modo USB. Reinicie e intentelo de nuevo.");
    } else {
      ui_show_usb_mode();
    }
    return;
  }

  logSection("Mount microSD and load CSV data");
  if (storage_sd_mount() != ESP_OK) {
    ui_show_fatal_error("No se detecta la tarjeta microSD. Reinicie.");
    return;
  }
  /* ANTES que nada: si un corte de corriente pillo una reescritura de
   * datos_maestros.csv a medias, recuperarlo. El orden es critico y va
   * justo aqui, delante de sd_provision: si se hiciera despues, este ya
   * habria creado un fichero vacio con solo la cabecera al no encontrarlo,
   * y la reparacion daria por bueno ese fichero vacio y borraria el .tmp
   * con los datos reales. */
  csv_master_recover_interrupted_rewrite(DATOS_MAESTROS_PATH);

  /* Solo crea datos_maestros.csv (vacio, con cabecera) si no existe ya en
   * la SD; los articulos se dan de alta desde la app. */
  sd_provision_ensure_master_header(DATOS_MAESTROS_PATH);
  if (csv_master_load(DATOS_MAESTROS_PATH) != ESP_OK) {
    ui_show_fatal_error("No se pudo cargar datos_maestros.csv de la SD.");
    return;
  }
  if (csv_inventory_init(INVENTARIO_PATH) != ESP_OK) {
    ui_show_fatal_error("No se pudo abrir/crear inventario.csv en la SD.");
    return;
  }

  logSection("Start app tasks");
  app_events_init();
  app_fsm_start();

  if (nfc_task_start() != ESP_OK) {
    ESP_LOGE(TAG, "No se pudo iniciar el lector NFC (PN532)");
    ui_show_fatal_error("Fallo al iniciar el lector NFC. Revise el cableado y reinicie.");
    return;
  }
  if (scale_task_start() != ESP_OK) {
    ESP_LOGE(TAG, "No se pudo iniciar la UART de la bascula");
    ui_show_fatal_error("Fallo al iniciar la bascula. Revise el cableado y reinicie.");
    return;
  }

  logSection("LVGL porting example end");
}

void loop()
{
  ESP_LOGI(TAG, "IDLE loop");
  // delay(1000);
}