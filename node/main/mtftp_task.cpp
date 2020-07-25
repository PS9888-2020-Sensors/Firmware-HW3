#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_vfs_fat.h"
#include "esp_now.h"
#include "esp_log.h"

#include "mtftp_server.hpp"

#include "mtftp_task.h"
#include "common.h"

#include "sdkconfig.h"

// len("/sdcard/") + str(file_index) + null
// 8 + max 5 + 1
static const uint8_t LEN_MAX_FNAME = 14;

// maximum number of packets buffered in esp-now
// wifi alloc failure observed when > 32 are buffered
static const uint8_t MAX_BUFFERED_TX = 8;

static MtftpServer server;

enum state {
  STATE_WAIT_PEER,
  STATE_ACTIVE
};

struct {
  uint8_t peer_addr[6];
  volatile int8_t buffered_tx;
  enum state state;

  uint16_t file_index;
  FILE *fp;
  RingbufHandle_t buf_handle;
  // offset that data in buf_handle starts at in the file
  uint32_t buf_offset;
} local_state;

static bool readFile(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br) {
  const char *TAG = "readFile";

  ESP_LOGD(TAG, "readFile of %d at offset=%lu btr=%d", file_index, (unsigned long) file_offset, btr);

  size_t br_buf;
  uint8_t *ptr_buf;

  if (local_state.file_index != file_index) {
    if (local_state.file_index != 0) {
      ESP_LOGI(TAG, "fclose %d", local_state.file_index);
      fclose(local_state.fp);
    }

    // clear the ringbuffer by reading everything
    ptr_buf = (uint8_t *) xRingbufferReceiveUpTo(local_state.buf_handle, &br_buf, 0, CONFIG_READ_BUF_SIZE);
    if (ptr_buf != NULL) vRingbufferReturnItem(local_state.buf_handle, ptr_buf);

    ESP_LOGI(TAG, "purging read buffer (file index mismatch)");

    char fname[LEN_MAX_FNAME];

    snprintf(fname, LEN_MAX_FNAME, "%s/%d", SD_MOUNT_POINT, file_index);

    local_state.fp = fopen(fname, "r");
    if (local_state.fp == NULL) {
      ESP_LOGE(TAG, "fopen %s failed", fname);
      return false;
    }

    ESP_LOGI(TAG, "fopen %d", file_index);

    local_state.file_index = file_index;
  }

  if (local_state.buf_offset != file_offset) {
    // clear the ringbuffer by reading everything
    // if file_offset > buf_offset it might be possible to optimise by
    // dumping the leading part in the buffer, but we're only likely to go
    // backwards because of missing data packets anyway
    ptr_buf = (uint8_t *) xRingbufferReceiveUpTo(local_state.buf_handle, &br_buf, 0, CONFIG_READ_BUF_SIZE);
    if (ptr_buf != NULL) vRingbufferReturnItem(local_state.buf_handle, ptr_buf);

    ESP_LOGI(TAG, "purging read buffer (file offset mismatch)");
  }

  ptr_buf = (uint8_t *) xRingbufferReceiveUpTo(local_state.buf_handle, &br_buf, 0, btr);

  *br = 0;

  if (ptr_buf != NULL) {
    memcpy(data, ptr_buf, br_buf);
    vRingbufferReturnItem(local_state.buf_handle, ptr_buf);

    // since we've pulled out br_buf bytes from the rb, buf_offset has also incremented
    local_state.buf_offset += br_buf;

    *br = br_buf;
    if (br_buf == btr) {
      // if full block retrieved, return
      ESP_LOGD(TAG, "got %d from rb", br_buf);
      return true;
    }
  }

  // if failed to retrieve anything or less bytes returned than requested,
  // try to read more data

  uint32_t cur_file_offset = file_offset + (*br);

  if (fseek(local_state.fp, cur_file_offset, SEEK_SET) != 0) {
    ESP_LOGE(TAG, "fseek of %d to %d failed", file_index, cur_file_offset);
    return false;
  }

  uint16_t bytes_read;
  uint8_t *read_data = (uint8_t *) malloc(CONFIG_READ_BUF_SIZE);
  if (read_data == NULL) {
    ESP_LOGE(TAG, "malloc read_data failed");
    return false;
  }

  bytes_read = fread(read_data, 1, xRingbufferGetCurFreeSize(local_state.buf_handle), local_state.fp);

  if (xRingbufferSend(local_state.buf_handle, read_data, bytes_read, 0) == pdFALSE) {
    ESP_LOGE(TAG, "failed to send to rb");
    free(read_data);
    return false;
  }

  free(read_data);

  local_state.buf_offset = cur_file_offset;

  ptr_buf = (uint8_t *) xRingbufferReceiveUpTo(local_state.buf_handle, &br_buf, 0, btr - (*br));

  // if null at this point, should have hit end of file?
  if (ptr_buf == NULL) {
    ESP_LOGD(TAG, "got null after loading from SD");
    return true;
  }

  memcpy(data + (*br), ptr_buf, br_buf);
  *br += br_buf;

  vRingbufferReturnItem(local_state.buf_handle, ptr_buf);
  local_state.buf_offset += br_buf;

  return true;
}

static void sendEspNow(const uint8_t *data, uint8_t len) {
  while (local_state.buffered_tx > MAX_BUFFERED_TX);

  ESP_ERROR_CHECK(esp_now_send((const uint8_t *) local_state.peer_addr, data, len));
  local_state.buffered_tx ++;
}

static void onSendEspNowCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  local_state.buffered_tx --;
}

static void onRecvEspNowCb(const uint8_t *mac_addr, const uint8_t *data, int len) {
  const char *TAG = "onRecvEspNowCb";
  ESP_LOGD(TAG, "received packet from " FORMAT_MAC ", len=%d, data[0]=%02x", ARG_MAC(mac_addr), len, (unsigned int) data[0]);

  if (local_state.state == STATE_WAIT_PEER) {
    if (len == LEN_SYNC_PACKET && memcmp(data, SYNC_PACKET, LEN_SYNC_PACKET) == 0) {
      ESP_LOGI(TAG, "sync packet received");

      espnow_add_peer(mac_addr);

      // send back the same SYNC packet to the collector as ACK
      memcpy(local_state.peer_addr, mac_addr, 6);
      sendEspNow(data, len);

      local_state.state = STATE_ACTIVE;
    }
  } else if (local_state.state == STATE_ACTIVE) {
    if (memcmp(mac_addr, local_state.peer_addr, 6) == 0) {
      server.onPacketRecv(data, (uint16_t) len);
    } else {
      ESP_LOGD(TAG, "received packet from non peer");
    }
  }
  
  ESP_LOGD(TAG, "end");
}

static void endWindow(void) {
  const char *TAG = "endWindow";

  esp_now_del_peer(local_state.peer_addr);
  ESP_LOGI(TAG, "ending window");

  memset(local_state.peer_addr, 0, 6);
  local_state.state = STATE_WAIT_PEER;

  if (local_state.file_index != 0) {
    ESP_LOGI(TAG, "fclose %d", local_state.file_index);
    fclose(local_state.fp);

    local_state.file_index = 0;
  }
}

void mtftp_task(void *pvParameter) {
  memset(&local_state, 0, sizeof(local_state));

  esp_now_register_send_cb(onSendEspNowCb);
  esp_now_register_recv_cb(onRecvEspNowCb);

  server.init(&readFile, &sendEspNow);
  server.setOnIdleCb(&endWindow);

  local_state.buf_handle = xRingbufferCreate(CONFIG_READ_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);

  while(1) {
    server.loop();
    if (server.isIdle()) {
      vTaskDelay(1);
    }
  }
}
