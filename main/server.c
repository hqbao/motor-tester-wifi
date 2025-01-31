#include "server.h"

#include <math.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <string.h>
#include "platform.h"
#include "speed_control.h"

#define TAG "server.c"

#define ESC_PWM_LIMIT 10000 // microseconds
#define MAX_THRUST    1000000 // 1000 kg
#define MAX_WATT      1000000 // 1000 watt

#define MAX_PLAN_TIME 10000 // 10 seconds

#define MAX_CONTENT_SIZE 10240
httpd_handle_t web_httpd = NULL;
char g_http_content[MAX_CONTENT_SIZE] = {0,};

extern int g_thrust;
extern int g_voltage;
extern int g_current;
char g_running = 0;
int g_plan_idx = 0;
int g_plan[MAX_PLAN_TIME][4];

uint64_t t0 = 0;
int dt = 1;

void control_setup(void) {
  speed_control_init();
}

void set_speed(int pwm) {
  speed_control_set(pwm, pwm, pwm, pwm);
}

void control_loop(void) {
  uint64_t t1 = esp_timer_get_time();
  dt = t1 - t0;
  t0 = t1;

  if (g_running == 0) {
    set_speed(0);
    return;
  }

  int pwm = g_plan[g_plan_idx][0];
  set_speed(pwm);

  // Fill in the plan
  g_plan[g_plan_idx][1] = g_thrust;
  g_plan[g_plan_idx][2] = g_voltage;
  g_plan[g_plan_idx][3] = g_current;
  g_plan_idx++;

  if (g_plan_idx >= MAX_PLAN_TIME) {
    g_running = 0;
    ESP_LOGI(TAG, "Done");
  }
}

void on_reset(void) {
  g_running = 0;
  g_plan_idx = 0;
  ESP_LOGI(TAG, "Reset");

  // Reset plan
  for (int i = 0; i < MAX_PLAN_TIME; i++) {
    g_plan[i][0] = -1;
    g_plan[i][1] = -1;
    g_plan[i][2] = -1;
    g_plan[i][3] = -1;
  }
}

char on_start(void) {
  if (g_running) return 1;

  // Prepare test plan
  int pwm = g_plan[0][0];
  for (int i = 0; i < MAX_PLAN_TIME; i++) {
    if (g_plan[i][0] == -1) g_plan[i][0] = pwm;
    else if (g_plan[i][0] >= 0) pwm = g_plan[i][0];
  }

  ESP_LOGI(TAG, "Go");

  g_running = 1;
  g_plan_idx = 0;
  return 0;
}

void on_stop(void) {
  g_running = 0;
  g_plan_idx = 0;
  ESP_LOGI(TAG, "Stop");
}

void on_try(int pwm) {
  ESP_LOGI(TAG, "on_try: %d", pwm);
  for (int i = 0; i < MAX_PLAN_TIME; i++) {
    g_plan[i][0] = pwm;
  }

  g_running = 1;
  g_plan_idx = 0;
}

int on_plan_received(const char *data, int size) {
  // ESP_LOGI(TAG, "Plan size: [%d] %s", size, data);

  on_reset();

  char *p = (char *)data;
  int nums[2] = {-1, -1};
  char str_num[8];
  int str_num_idx = 0;
  memset(str_num, 0, 8);
  while (p - data < size) {
    if (*p == '\n') {
      nums[1] = atoi(str_num);
      if (nums[0] < 0 || nums[1] < 0 || nums[1] >= ESC_PWM_LIMIT) return -3;

      g_plan[nums[0]][0] = nums[1];
      ESP_LOGI(TAG, "Plan: %d -> %d", nums[0], nums[1]);

      str_num_idx = 0;
      memset(str_num, 0, 8);

      nums[0] = -1;
      nums[1] = -1;
    }
    else if (*p == '\t') {
      nums[0] = atoi(str_num);
      if (nums[0] < 0 || nums[0] >= MAX_PLAN_TIME) return -2;

      str_num_idx = 0;
      memset(str_num, 0, 8);
    }
    else {
      if (str_num_idx >= 8) return -1;
      str_num[str_num_idx++] = *p;
    }

    p++;
  }

  return 0;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  if (req->content_len >= MAX_CONTENT_SIZE) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too long");
  }

  int ret = httpd_req_recv(req, g_http_content, req->content_len);
  if (ret <= 0) return ESP_FAIL;
  // ESP_LOGI(TAG, "%s", g_http_content);

  if (memcmp(g_http_content, "reset", 5) == 0) {
    on_reset();
  }
  else if (memcmp(g_http_content, "start", 4) == 0) {
    char started = on_start();
    if (started == 1) {
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "In progress");
    }
  }
  else if (memcmp(g_http_content, "stop", 4) == 0) {
    on_stop();
  }
  else {
    int pwm = atoi(g_http_content);
    on_try(pwm);
  }

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 3);
}

static esp_err_t test_plan_handler(httpd_req_t *req) {
  if (req->content_len >= MAX_CONTENT_SIZE) {
    return httpd_resp_send_500(req);
  }

  int ret = httpd_req_recv(req, g_http_content, req->content_len);
  if (ret <= 0) return ESP_FAIL;
  // ESP_LOGI(TAG, "%s", g_http_content);

  ret = on_plan_received(g_http_content, req->content_len);
  if (ret < 0) {
    ESP_LOGI(TAG, "Invalid format %d", ret);
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid format");
  }

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 3);
}

static esp_err_t get_result_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/x-binary");

  int size = g_plan_idx*5*sizeof(int);
  char *data = (char*)malloc(size);
  if (data == NULL) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");;
  }

  for (int i = 0; i < g_plan_idx; i++) {
    memcpy(&data[i*20 + 0], (char*)&i, 4); // Time
    memcpy(&data[i*20 + 4], (char*)&g_plan[i][0], 4); // PWM
    memcpy(&data[i*20 + 8], (char*)&g_plan[i][1], 4); // Thrust
    memcpy(&data[i*20 + 12], (char*)&g_plan[i][2], 4); // Volt
    memcpy(&data[i*20 + 16], (char*)&g_plan[i][3], 4); // Current
  }

  ESP_LOGI(TAG, "Size %d", size);
  int sent = 0;
  while (sent < size) {
    int chunk_size = size - sent >= 1024 ? 1024 : size - sent;
    if (httpd_resp_send_chunk(req, &data[sent], chunk_size) != ESP_OK) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
      return ESP_FAIL;
    }

    sent += chunk_size;
    ESP_LOGI(TAG, "Sent %d", sent);
  }

  httpd_resp_send_chunk(req, NULL, 0);

  if (size > 0) {
    free(data);
  }

  return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "It works", 9);
}

void server_start(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };

  httpd_uri_t cmd_uri = {
    .uri = "/cmd",
    .method = HTTP_POST,
    .handler = cmd_handler,
    .user_ctx = NULL
  };

  httpd_uri_t test_plan_uri = {
    .uri = "/test-plan",
    .method = HTTP_POST,
    .handler = test_plan_handler,
    .user_ctx = NULL
  };

  httpd_uri_t get_result_uri = {
    .uri = "/result",
    .method = HTTP_GET,
    .handler = get_result_handler,
    .user_ctx = NULL
  };

  ESP_LOGI(TAG, "Starting web server on port: %d", config.server_port);
  if (httpd_start(&web_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(web_httpd, &index_uri);
    httpd_register_uri_handler(web_httpd, &cmd_uri);
    httpd_register_uri_handler(web_httpd, &test_plan_uri);
    httpd_register_uri_handler(web_httpd, &get_result_uri);
  }
}
