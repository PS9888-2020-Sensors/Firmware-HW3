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
#include "freertos/semphr.h"
#include "esp_log.h"

#include "sample.h"
#include "board.h"

extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t bin_end[]   asm("_binary_ulp_main_bin_end");

volatile uint32_t c = 0;

static void ulp_isr(void *arg) {
  SemaphoreHandle_t done = (SemaphoreHandle_t) arg;
  xSemaphoreGiveFromISR(done, NULL);
  c ++;
}

void start_ulp_program(void) {
  ESP_ERROR_CHECK(ulp_load_binary(0, bin_start, (bin_end - bin_start) / sizeof(uint32_t)));

  SemaphoreHandle_t ulp_isr_sem = xSemaphoreCreateBinary();

  gpio_num_t gpio_num = GPIO_NUM_39;

  ESP_ERROR_CHECK(rtc_gpio_init(gpio_num));
  ESP_ERROR_CHECK(rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY));
  ESP_ERROR_CHECK(rtc_gpio_hold_en(gpio_num));

  ESP_ERROR_CHECK(rtc_isr_register(&ulp_isr, (void*) ulp_isr_sem, RTC_CNTL_ULP_CP_INT_ENA_M));
  // enable rtc interrupt
  REG_SET_BIT(RTC_CNTL_INT_ENA_REG, RTC_CNTL_ULP_CP_INT_ENA_M);
  ESP_ERROR_CHECK(ulp_set_wakeup_period(0, 1000));

  ESP_ERROR_CHECK(ulp_run(&ulp_entry - RTC_SLOW_MEM));

  while(1) {
    // if (xSemaphoreTake(ulp_isr_sem, 1000 / portTICK_PERIOD_MS) == pdPASS) {
    //   ESP_LOGI("ulp", "isr triggered! %d", c);
    // }
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
    ESP_LOGI("ulp", "%d, %lld", c, time_us);
    c = 0;
    vTaskDelay(100);
  }
}