#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common.h"

#include "monitor_task.h"

bool shutdown = false;

void monitor_task(void *pvParameter) {
  uint64_t btn_press_time = 0;

  while(1) {
    if (get_btn_user() == 0) {
      if (btn_press_time == 0) {
        btn_press_time = esp_timer_get_time();
      }

      if ((esp_timer_get_time() - btn_press_time) > CONFIG_BTN_SHUTDOWN_TIME) {
        shutdown = true;
      }
    } else {
      btn_press_time = 0;
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}