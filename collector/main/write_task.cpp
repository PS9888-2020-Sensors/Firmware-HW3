#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "common.h"
#include "write_task.h"

SemaphoreHandle_t buffer_empty;

// used to lock access to write_buffer
SemaphoreHandle_t buffer_update;
RingbufHandle_t write_buffer;

FILE *fp;
uint16_t file_index = 0;
uint32_t base_file_offset = 0;

static const char *TAG = "write_task";

static void wait_for_close(void) {
  ESP_LOGI(TAG, "waiting for %d to close", file_index);
  if (xRingbufferGetCurFreeSize(write_buffer) > 0) {
    // try take the semphr to clear it
    // because its only given on empty, but not taken on not empty
    xSemaphoreTake(buffer_empty, 0);
  }

  xSemaphoreTake(buffer_empty, portMAX_DELAY);
  ESP_LOGI(TAG, "closed");
}

bool write_sd(uint16_t _file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw) {
  if (file_index != _file_index) {
    if (file_index != 0) {
      wait_for_close();
    }

    char fname[LEN_MAX_FNAME];
    snprintf(fname, LEN_MAX_FNAME, "%s/%d", SD_MOUNT_POINT, _file_index);

    // `r+` is used here because `a` does not allow writing to the middle of the file
    // but `r+` fails if the file does not exist, so open in `w` (create new) if so
    fp = fopen(fname, "r+");
    if (fp == NULL) {
      fp = fopen(fname, "w");
      if (fp == NULL) {
        ESP_LOGE(TAG, "fopen %s failed", fname);
        return false;
      }
    }

    ESP_LOGI(TAG, "fopen %d", _file_index);

    file_index = _file_index;
    base_file_offset = file_offset;
  }

  xSemaphoreTake(buffer_update, portMAX_DELAY);
  uint32_t cur_offset = base_file_offset + (CONFIG_WRITE_BUF_SIZE * 2 - xRingbufferGetCurFreeSize(write_buffer));
  if (cur_offset != file_offset) {
    ESP_LOGW(TAG, "offset mismatch: writing to %d but cur is %d", file_offset, cur_offset);
    xSemaphoreGive(buffer_update);
    return false;
  }

  xRingbufferSend(write_buffer, data, btw, portMAX_DELAY);
  xSemaphoreGive(buffer_update);

  return true;
}

void write_task(void *pvParameter) {
  buffer_empty = xSemaphoreCreateBinary();
  buffer_update = xSemaphoreCreateBinary();
  xSemaphoreGive(buffer_update);
  write_buffer = xRingbufferCreate(CONFIG_WRITE_BUF_SIZE * 2, RINGBUF_TYPE_BYTEBUF);

  size_t size;

  while(1) {
    uint8_t *buf = (uint8_t *) xRingbufferReceiveUpTo(write_buffer, &size, 100 / portTICK_PERIOD_MS, CONFIG_WRITE_BUF_SIZE);

    if (buf == NULL) {
      if (file_index == 0) continue;

      ESP_LOGI(TAG, "fclose %d", file_index);
      fclose(fp);
      file_index = 0;

      xSemaphoreGive(buffer_empty);
      continue;
    }

    write(fileno(fp), buf, size);

    xSemaphoreTake(buffer_update, portMAX_DELAY);
    vRingbufferReturnItem(write_buffer, buf);
    base_file_offset += size;
    xSemaphoreGive(buffer_update);
  }
}
