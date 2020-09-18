#include <stdio.h>
#include <sys/time.h>
#include "driver/rtc_cntl.h"
#include "soc/rtc_cntl_reg.h"
#include "esp32/ulp.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "ulp_main.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "sensor.h"

#include "sample_task.h"
#include "board.h"

extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t bin_end[]   asm("_binary_ulp_main_bin_end");

static const char *TAG = "sample";

TaskHandle_t sample_task_handle;
TaskHandle_t sample_write_task_handle;

char *sample_buffers[2];
uint8_t cur_buf = 0;

static void ulp_isr(void *arg) {
  xTaskNotify(sample_task_handle, 0, eNoAction);
}

static void sample_write_task(void *pvParameter) {
  // initialise buffers (in external PSRAM)
  for(uint8_t i = 0; i < 2; i++) {
    sample_buffers[i] = (char *) heap_caps_malloc(CONFIG_SAMPLE_BUFFER_NUM * sizeof(float), MALLOC_CAP_SPIRAM);
  }

  while(1) {
    uint32_t buf_index;
    xTaskNotifyWait(0, 0, &buf_index, portMAX_DELAY);
    ESP_LOGI(TAG, "writing buffer %d to file", buf_index);
  }
}

void sample_task(void *pvParameter) {
  sample_task_handle = xTaskGetCurrentTaskHandle();
  xTaskCreate(sample_write_task, "sample_write_task", 4096, NULL, 5, &sample_write_task_handle);

  ESP_ERROR_CHECK(ulp_load_binary(0, bin_start, (bin_end - bin_start) / sizeof(uint32_t)));

  gpio_num_t gpio_num = GPIO_NUM_39;

  ESP_ERROR_CHECK(rtc_gpio_init(gpio_num));
  ESP_ERROR_CHECK(rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY));
  ESP_ERROR_CHECK(rtc_gpio_hold_en(gpio_num));

  ESP_ERROR_CHECK(rtc_isr_register(&ulp_isr, NULL, RTC_CNTL_ULP_CP_INT_ENA_M));
  // enable rtc interrupt
  REG_SET_BIT(RTC_CNTL_INT_ENA_REG, RTC_CNTL_ULP_CP_INT_ENA_M);
  ESP_ERROR_CHECK(ulp_set_wakeup_period(0, CONFIG_SAMPLE_PERIOD));

  ESP_ERROR_CHECK(ulp_run(&ulp_entry - RTC_SLOW_MEM));

  sensor_init();

  bool started_sampling = false;
  while(1) {
    xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);

    if (started_sampling) {
      float val = sensor_read();
      if (xRingbufferSend(sample_buffers[cur_buf], &val, sizeof(float), 0) == pdFALSE) {
        xTaskNotify(sample_write_task_handle, cur_buf, eSetValueWithOverwrite);

        // write current value to other buffer
        cur_buf = !cur_buf;
        if (xRingbufferSend(sample_buffers[cur_buf], &val, sizeof(float), 0) == pdFALSE) {
          ESP_LOGW(TAG, "value lost: could not write to either buffer");
        }
      }
    }

    started_sampling = true;
    sensor_start_read();
  }
}