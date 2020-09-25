#include <sys/unistd.h>
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "common.h"
#include "write_task.h"

SemaphoreHandle_t start_write;
SemaphoreHandle_t buffer_empty;

// used to lock access to write_buffer
SemaphoreHandle_t buffer_update;
RingbufHandle_t write_buffer;

FILE *fp;
uint16_t file_index = 0;
uint32_t base_file_offset = 0;

static const char *TAG = "write_task";

void wait_for_close(void) {
  if (file_index == 0) return;

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

    if (lseek(fileno(fp), file_offset, SEEK_SET) == -1) {
      ESP_LOGW(TAG, "failed to seek to %d", file_offset);
      fclose(fp);
      return false;
    }

    file_index = _file_index;
    base_file_offset = file_offset;
  }

  while(1) {
    xSemaphoreTake(buffer_update, portMAX_DELAY);
    uint32_t buffer_count = CONFIG_WRITE_BUF_SIZE * 2 - xRingbufferGetCurFreeSize(write_buffer);
    uint32_t cur_offset = base_file_offset + buffer_count;
    if (cur_offset != file_offset) {
      ESP_LOGW(TAG, "offset mismatch: writing to %d but cur is %d", file_offset, cur_offset);
      xSemaphoreGive(buffer_update);
      return false;
    }

    if (xRingbufferSend(write_buffer, data, btw, 0) == pdTRUE) {
      // if this fails, we have to give up because the buffer cant get cleared
      // until we release buffer_update
      xSemaphoreGive(buffer_update);

      // if we have more than CONFIG_WRITE_BUF_SIZE, signal to start write
      if (buffer_count + btw > CONFIG_WRITE_BUF_SIZE) {
        xSemaphoreGive(start_write);
      }
      return true;
    }
    xSemaphoreGive(buffer_update);
  }
}

void write_task(void *pvParameter) {
  buffer_empty = xSemaphoreCreateBinary();
  buffer_update = xSemaphoreCreateBinary();
  start_write = xSemaphoreCreateBinary();
  xSemaphoreGive(buffer_update);
  write_buffer = xRingbufferCreate(CONFIG_WRITE_BUF_SIZE * 2, RINGBUF_TYPE_BYTEBUF);

  // allocate yet another buffer to actually hold data right before writing to SD because
  // the ring buffer may not store stuff sequentially
  char *write_buf = (char *) malloc(CONFIG_WRITE_BUF_SIZE);
  uint32_t write_buf_count = 0;

  size_t size;

  while(1) {
    xSemaphoreTake(start_write, 500 / portTICK_PERIOD_MS);

    uint8_t *buf = (uint8_t *) xRingbufferReceiveUpTo(write_buffer, &size, 0, CONFIG_WRITE_BUF_SIZE);

    if (buf == NULL) {
      if (file_index == 0) continue;

      ESP_LOGI(TAG, "fclose %d", file_index);
      fclose(fp);
      file_index = 0;

      xSemaphoreGive(buffer_empty);
      continue;
    }

    memcpy(write_buf, buf, size);
    write_buf_count = size;

    xSemaphoreTake(buffer_update, portMAX_DELAY);
    vRingbufferReturnItem(write_buffer, buf);
    base_file_offset += size;
    
    // read more because the ringbuffer may not store sequentially
    if (size < CONFIG_WRITE_BUF_SIZE) {
      buf = (uint8_t *) xRingbufferReceiveUpTo(write_buffer, &size, 0, CONFIG_WRITE_BUF_SIZE - write_buf_count);
      if (buf != NULL) {
        memcpy(write_buf + write_buf_count, buf, size);
        write_buf_count += size;
        vRingbufferReturnItem(write_buffer, buf);
        base_file_offset += size;
      }
    }

    xSemaphoreGive(buffer_update);

    ESP_LOGI(TAG, "writing %d bytes", write_buf_count);
    write(fileno(fp), write_buf, write_buf_count);
  }
}
