#include <stdio.h>
#include <sys/time.h>
#include <dirent.h>
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
#include "common.h"
#include "board.h"

uint16_t sample_file_index;
SemaphoreHandle_t sample_file_semaph;
SemaphoreHandle_t time_acquired_semaph;

extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t bin_end[]   asm("_binary_ulp_main_bin_end");

static const char *TAG = "sample";

TaskHandle_t sample_task_handle;
TaskHandle_t sample_write_task_handle;

char *sample_buffers[2];
uint32_t sample_count[2] = {0};
uint64_t sample_start_time[2] = {0};
uint8_t cur_buf = 0;

static void ulp_isr(void *arg) {
  xTaskNotify(sample_task_handle, 0, eNoAction);
}

static uint16_t get_largest_file(void) {
  const char *TAG = "get_largest_file";

  uint16_t largest = 0;

  DIR *d = opendir(SD_MOUNT_POINT);
  if (d == NULL) {
    ESP_LOGW(TAG, "failed to open %s", SD_MOUNT_POINT);
    return largest;
  }

  struct dirent *dir;

  while ((dir = readdir(d)) != NULL) {
    if (dir->d_type != DT_REG) continue;

    uint16_t num;
    if (!conv_strtoul(dir->d_name, &num)) continue;

    if (num > largest) largest = num;
  }

  closedir(d);
  return largest;
}

static void sample_write_task(void *pvParameter) {
  // initialise buffers (in external PSRAM)
  for(uint8_t i = 0; i < 2; i++) {
    sample_buffers[i] = (char *) heap_caps_malloc(CONFIG_SAMPLE_BUFFER_NUM * sizeof(TYPE_SENSOR_READING), MALLOC_CAP_SPIRAM);
  }

  sample_file_index = get_largest_file();

  if (sample_file_index == 0) {
    sample_file_index = 1;
  } else {
    sample_file_index ++;
  }

  ESP_LOGI(TAG, "samples will be written to file_index=%d", sample_file_index);

  char * file_buffer = (char *) malloc(CONFIG_WRITE_BUF_SIZE);

  assert(file_buffer != NULL);

  struct __attribute__((__packed__)) {
    char header[3] = {'H', 'D', 'R'};
    uint64_t timestamp;
    uint8_t sample_size;
    uint32_t sample_count;
  } chunk_header;

  while(1) {
    uint32_t buf_index;
    xTaskNotifyWait(0, 0, &buf_index, portMAX_DELAY);
    ESP_LOGI(TAG, "writing buffer %d to file", buf_index);

    if (xSemaphoreTake(sample_file_semaph, 0) == pdFALSE) {
      ESP_LOGI(TAG, "waiting to acquire semaphore");
      // TODO: make this timeout, if fail to acquire then write to
      // sample_file_index ++
      xSemaphoreTake(sample_file_semaph, portMAX_DELAY);
      ESP_LOGI(TAG, "taken semaphore");
    }

    char fname[LEN_MAX_FNAME];
    snprintf(fname, LEN_MAX_FNAME, "%s/%d", SD_MOUNT_POINT, sample_file_index);

    FILE *fp = fopen(fname, "a");
    if (fp == NULL) {
      ESP_LOGE(TAG, "fopen %s failed", fname);
      continue;
    }

    if (setvbuf(fp, file_buffer, _IOFBF, CONFIG_WRITE_BUF_SIZE) != 0) {
      ESP_LOGE(TAG, "setvbuf failed");
      continue;
    }

    chunk_header.timestamp = sample_start_time[buf_index];
    chunk_header.sample_size = sizeof(TYPE_SENSOR_READING);
    chunk_header.sample_count = sample_count[buf_index] + 1;

    fwrite(&chunk_header, sizeof(chunk_header), 1, fp);
    fwrite(sample_buffers[buf_index], sizeof(TYPE_SENSOR_READING), sample_count[buf_index] + 1, fp);

    fclose(fp);
    ESP_LOGI(TAG, "write done");
    xSemaphoreGive(sample_file_semaph);

    sample_count[buf_index] = 0;
  }
}

void sample_task(void *pvParameter) {
  sample_file_semaph = xSemaphoreCreateBinary();
  // initialise to 1
  xSemaphoreGive(sample_file_semaph);
  time_acquired_semaph = xSemaphoreCreateBinary();

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

#ifndef CONFIG_START_WITHOUT_TIME_SYNC
  ESP_LOGI(TAG, "waiting for time sync");
  xSemaphoreTake(time_acquired_semaph, portMAX_DELAY);
  ESP_LOGI(TAG, "starting sampling");
#else
  ESP_LOGW(TAG, "skipping wait for time sync because CONFIG_START_WITHOUT_TIME_SYNC is set");
#endif

  ESP_ERROR_CHECK(ulp_run(&ulp_entry - RTC_SLOW_MEM));

  sensor_init();

  bool started_sampling = false;
  while(1) {
    xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);

    if (started_sampling) {
      TYPE_SENSOR_READING val = sensor_read();
      *(((TYPE_SENSOR_READING *) sample_buffers[cur_buf]) + sample_count[cur_buf]) = val;
      if (sample_count[cur_buf] == 0) {
        sample_start_time[cur_buf] = get_time();
      }

      if (sample_count[cur_buf] == (CONFIG_SAMPLE_BUFFER_NUM - 1)) {
        // notify other task to start write
        xTaskNotify(sample_write_task_handle, cur_buf, eSetValueWithOverwrite);

        // start using other buffer
        cur_buf = !cur_buf;
      } else {
        sample_count[cur_buf] ++;
      }
    }

    started_sampling = true;
    sensor_start_read();
  }
}
