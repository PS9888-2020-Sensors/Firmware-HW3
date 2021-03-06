#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_now.h"
#include "esp_log.h"

#include "common.h"
#include "mtftp_task.h"

#include "sdkconfig.h"

const char *TAG = "main";

extern "C" {
  void app_main();
}

void app_main(void) {
  hw_init();

  nvs_init();
  wifi_init();
  espnow_init();

  sd_init();

  xTaskCreate(mtftp_task, "mtftp_task", 2048, NULL, 4, NULL);
}
