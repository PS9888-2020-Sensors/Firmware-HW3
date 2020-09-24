#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"

#include "write_task.h"

SemaphoreHandle_t buffer_empty;

RingbufHandle_t write_buffer;
uint16_t file_index = 0;
uint32_t base_file_offset = 0;

bool write_sd(uint16_t _file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw) {
  if (file_index != _file_index) {
    wait_for_close();
    // open fp
  }

  // send to rb
}

static void wait_for_close(void) {
  if (xRingbufferGetCurFreeSize(write_buffer) > 0) {
    // try take the semphr to clear it
    // because its only given on empty, but not taken on not empt
    xSemaphoreTake(buffer_empty, 0);
  }

  xSemaphoreTake(buffer_empty, portMAX_DELAY);
}

void write_task(void *pvParameter) {
  buffer_empty = xSemaphoreCreateBinary();
  write_buffer = xRingbufferCreate(CONFIG_WRITE_BUF_SIZE * 2, RINGBUF_TYPE_BYTEBUF);

  size_t size;

  while(1) {
    uint8_t *buf = (uint8_t *) xRingbufferReceiveUpTo(write_buffer, &size, 100 / portTICK_PERIOD_MS, CONFIG_WRITE_BUF_SIZE);

    if (buf == NULL) {
      // close fp here
      xSemaphoreGive(buffer_empty);
      continue;
    }

    // write to fp here
  }
}