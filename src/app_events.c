#include "app_events.h"

#include "esp_log.h"

static const char *TAG = "app_events";

QueueHandle_t g_app_event_queue = NULL;

void app_events_init(void)
{
    if (g_app_event_queue != NULL) {
        return;
    }
    g_app_event_queue = xQueueCreate(8, sizeof(app_event_t));
}

void app_events_post(const app_event_t *event)
{
    if (g_app_event_queue == NULL) {
        return;
    }
    if (xQueueSend(g_app_event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Cola de eventos llena, evento %d descartado", (int)event->type);
    }
}
