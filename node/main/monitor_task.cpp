#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "common.h"

#include "monitor_task.h"

QueueHandle_t evt_queue = xQueueCreate(4, 1);
bool shutdown = false;

// on, period (us)
const uint32_t led_blink[][2] = {
  {1000000, 2000000},    // slow blink - waiting for time sync
  {50000, 3000000},      // periodic blink - standard mode when everything inactive except sampling
  {100000, 500000},      // fast blink - when transfer
  {1000000, 1000000}     // full on - shutdown
};

uint8_t blink_index = 0;

void monitor_task(void *pvParameter) {
  uint64_t btn_press_time = 0;
  bool has_time_sync = false;

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

    uint8_t evt;
    if (xQueueReceive(evt_queue, &evt, 50 / portTICK_PERIOD_MS) == pdTRUE) {
      if (evt == EVT_TIME_SYNCED) {
        blink_index = 1;
        has_time_sync = true;
      } else if (evt == EVT_COMMS_START) {
        blink_index = 2;
      } else if (evt == EVT_COMMS_END) {
        if (has_time_sync) {
          blink_index = 1;
        } else {
          blink_index = 0;
        }
      } else if (evt == EVT_SHUTDOWN_WRITE_DONE) {
        blink_index = 3;
      }
    }

    int64_t time = esp_timer_get_time();

    if ((time % led_blink[blink_index][1]) < led_blink[blink_index][0]) {
      set_led(1);
    } else {
      set_led(0);
    }
  }
}
