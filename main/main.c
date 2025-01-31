#include <stdio.h>
#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <string.h>
#include "platform.h"
#include "server.h"

#define TAG "main.c"

#define TAG "main.c"
#define SSID "Bao"
#define PASS "nopassword"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

TaskHandle_t task_hangle_1 = NULL;
TaskHandle_t task_hangle_2 = NULL;

static EventGroupHandle_t s_wifi_event_group;

int g_thrust = 0;
int g_voltage = 0;
int g_current = 0;

void core0();
void core1();

void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
int64_t get_time(void) { return esp_timer_get_time(); }
uint32_t millis(void) { return (uint32_t)( esp_timer_get_time()/1000); }
void flash(uint8_t count) {}

void app_main(void) {
  printf("Start program\n");

  xTaskCreatePinnedToCore(core0, "Core 0", 4096, NULL, 10, &task_hangle_1, 0);
  xTaskCreatePinnedToCore(core1, "Core 1", 4096, NULL, 10, &task_hangle_2, 1);
  while (1) { delay(1000); }
}

void timer_control(void *param) {
  control_loop();
}

void core0() {
  control_setup();

  // Start timer 1
  const esp_timer_create_args_t timer_args1 = {
    .callback = &timer_control,
    .name = "Control Timer"
  };
  esp_timer_handle_t timer_handler1;
  esp_timer_create(&timer_args1, &timer_handler1);
  esp_timer_start_periodic(timer_handler1, 1000000/CONTROL_FREQ);
  ESP_LOGI(TAG, "Control timer ready");

  // Measure thrust, volt and current here
  while (1) {
    delay(1000);
  }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  static int s_retry_num = 0;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } 
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < 100) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "Retry to connect to the AP");
    } 
    else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG,"connect to the AP fail");
  } 
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

static void wifi_connect(void) {
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config = {
    .sta = {
      .ssid = SSID,
      .password = PASS,
    },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
  ESP_ERROR_CHECK(esp_wifi_start() );

  ESP_LOGI(TAG, "wifi_init_sta finished.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
   * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(
    s_wifi_event_group,
    WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
    pdFALSE,
    pdFALSE,
    portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
   * happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", SSID, PASS);
    server_start();
  } 
  else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", SSID, PASS);
  }
  else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }
}

void core1() {
  //Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  wifi_connect();
  while (1) {delay(1000);}
}
