
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

#include "secrets.h"

#define WIFI_CONNECTED_BIT BIT0
#define SENSOR_PERIOD_MS   2000
#define MQTT_TOPIC         "nitha/esp32/sensors"

static const char *TAG = "RTOS_NODE";

typedef struct {
    float temperature;
    float humidity;
    float ax;
    float ay;
    float az;
    uint32_t timestamp_ms;
} sensor_data_t;

static QueueHandle_t display_queue;
static QueueHandle_t mqtt_queue;
static EventGroupHandle_t wifi_events;

static esp_mqtt_client_handle_t mqtt_client;
static volatile bool mqtt_connected = false;

/* ---------- Wi-Fi events ---------- */

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(
            wifi_events, WIFI_CONNECTED_BIT);

        ESP_LOGW(TAG, "Wi-Fi disconnected");
        esp_wifi_connect();
    }

    if (event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(
            wifi_events, WIFI_CONNECTED_BIT);

        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

/* ---------- Wi-Fi initialization ---------- */

static void wifi_init(void)
{
    wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        wifi_event_handler, NULL));

    wifi_config_t config = {0};

    snprintf((char *)config.sta.ssid,
             sizeof(config.sta.ssid),
             "%s", WIFI_SSID);

    snprintf((char *)config.sta.password,
             sizeof(config.sta.password),
             "%s", WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(
        esp_wifi_set_config(WIFI_IF_STA, &config));

    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ---------- MQTT events ---------- */

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT connected");
            break;

        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            break;

        default:
            break;
    }
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t config = {
        .broker.address.uri = MQTT_BROKER_URI
    };

    mqtt_client = esp_mqtt_client_init(&config);

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL));

    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
}

/* ---------- Task 1: Sensor task ---------- */

static void sensor_task(void *arg)
{
    sensor_data_t data = {0};

    while (1) {
        /*
         * Simulated sensor values.
         * Replace with DHT11 and MPU6050 readings
         * after the initial firmware build works.
         */
        data.temperature = 28.5f;
        data.humidity = 60.0f;
        data.ax = 0.10f;
        data.ay = 0.20f;
        data.az = 9.75f;

        data.timestamp_ms =
            (uint32_t)(xTaskGetTickCount()
                       * portTICK_PERIOD_MS);

        /* Send a copy to each consumer. */
        xQueueOverwrite(display_queue, &data);
        xQueueOverwrite(mqtt_queue, &data);

        ESP_LOGI(TAG,
                 "Sample: T=%.1f C, H=%.1f%%",
                 data.temperature, data.humidity);

        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

/* ---------- Task 2: Display task ---------- */

static void display_task(void *arg)
{
    sensor_data_t data;

    while (1) {
        if (xQueueReceive(
                display_queue, &data,
                portMAX_DELAY) == pdTRUE) {

            /* OLED driver will be added later. */
            ESP_LOGI(TAG,
                     "Display: T=%.1f H=%.1f Ax=%.2f",
                     data.temperature,
                     data.humidity,
                     data.ax);
        }
    }
}

/* ---------- Task 3: MQTT task ---------- */

static void mqtt_task(void *arg)
{
    sensor_data_t data;
    char payload[192];

    xEventGroupWaitBits(
        wifi_events,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    mqtt_init();

    while (1) {
        if (xQueueReceive(
                mqtt_queue, &data,
                portMAX_DELAY) == pdTRUE) {

            if (!mqtt_connected) {
                continue;
            }

            snprintf(
                payload, sizeof(payload),
                "{\"temp\":%.1f,\"hum\":%.1f,"
                "\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
                "\"time_ms\":%lu}",
                data.temperature,
                data.humidity,
                data.ax,
                data.ay,
                data.az,
                (unsigned long)data.timestamp_ms
            );

            int msg_id = esp_mqtt_client_publish(
                mqtt_client,
                MQTT_TOPIC,
                payload,
                0,
                1,
                0
            );

            if (msg_id >= 0) {
                ESP_LOGI(TAG, "Published: %s", payload);
            } else {
                ESP_LOGW(TAG, "MQTT publish failed");
            }
        }
    }
}

/* ---------- Main application ---------- */

void app_main(void)
{
    esp_err_t result = nvs_flash_init();

    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }

    ESP_ERROR_CHECK(result);

    display_queue = xQueueCreate(1, sizeof(sensor_data_t));
    mqtt_queue = xQueueCreate(1, sizeof(sensor_data_t));

    if (display_queue == NULL || mqtt_queue == NULL) {
        ESP_LOGE(TAG, "Queue creation failed");
        return;
    }

    wifi_init();

    xTaskCreate(sensor_task, "sensor_task",
                3072, NULL, 3, NULL);

    xTaskCreate(display_task, "display_task",
                3072, NULL, 2, NULL);

    xTaskCreate(mqtt_task, "mqtt_task",
                4096, NULL, 2, NULL);
}
