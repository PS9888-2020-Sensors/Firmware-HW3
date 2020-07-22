#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_fat.h"
#include "esp_now.h"
#include "esp_log.h"

#include "mtftp_server.hpp"

#include "mtftp_task.h"
#include "common.h"

// len("/sdcard/") + str(file_index) + null
// 8 + max 5 + 1
static const uint8_t LEN_MAX_FNAME = 14 + 4;

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
  int8_t buffered_tx;
  enum state state;
} local_state;

static bool readFile(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br) {
  const char *TAG = "readFile";
  char fname[LEN_MAX_FNAME];

  FILE *fp;

  snprintf(fname, LEN_MAX_FNAME, "%s/%d.txt", SD_MOUNT_POINT, file_index);

  ESP_LOGI(TAG, "fopen %s", fname);

  fp = fopen(fname, "r");
  if (fp == NULL) {
    ESP_LOGE(TAG, "fopen failed: %d", ferror(fp));
    return false;
  }

  if (fseek(fp, file_offset, SEEK_SET) != 0) {
    ESP_LOGE(TAG, "fseek failed: %d", ferror(fp));
    return false;
  }

  *br = fread(data, 1, btr, fp);

  fclose(fp);

  return true;
}

static void sendEspNow(const uint8_t *data, uint8_t len) {
  const char *TAG = "sendEspNow";
  ESP_LOGD(TAG, "called with len=%d", len);

  while (local_state.buffered_tx > MAX_BUFFERED_TX) {
    vTaskDelay(1);
  }

  ESP_ERROR_CHECK(esp_now_send((const uint8_t *) local_state.peer_addr, data, len));
  local_state.buffered_tx ++;
}

static void onSendEspNowCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  local_state.buffered_tx --;
}

static void onRecvEspNowCb(const uint8_t *mac_addr, const uint8_t *data, int len) {
  const char *TAG = "onRecvEspNowCb";
  ESP_LOGD(TAG, "received packet of len %d:", len);
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, mac_addr, 6, ESP_LOG_DEBUG);

  if (local_state.state == STATE_WAIT_PEER) {
    if (len == LEN_SYNC_PACKET && memcmp(data, SYNC_PACKET, LEN_SYNC_PACKET) == 0) {
      ESP_LOGI(TAG, "sync packet received from:");
      ESP_LOG_BUFFER_HEX_LEVEL(TAG, mac_addr, 6, ESP_LOG_INFO);

      espnow_add_peer(mac_addr);

      // send back the same SYNC packet to the collector as ACK
      memcpy(local_state.peer_addr, mac_addr, 6);
      sendEspNow(data, len);

      local_state.state = STATE_ACTIVE;
    }
  } else if (local_state.state == STATE_ACTIVE) {
    if (memcmp(mac_addr, local_state.peer_addr, 6) == 0) {
      server.onPacketRecv(data, (uint16_t) len);
    } {
      ESP_LOGD(TAG, "received packet from non peer:");
      ESP_LOG_BUFFER_HEX_LEVEL(TAG, mac_addr, 6, ESP_LOG_DEBUG);
    }
  }
}

static void endWindow(void) {
  const char *TAG = "endWindow";

  esp_now_del_peer(local_state.peer_addr);
  ESP_LOGI(TAG, "ending window");

  memset(local_state.peer_addr, 0, 6);
  local_state.state = STATE_WAIT_PEER;
}

void mtftp_task(void *pvParameter) {
  memset(&local_state, 0, sizeof(local_state));

  esp_now_register_send_cb(onSendEspNowCb);
  esp_now_register_recv_cb(onRecvEspNowCb);

  server.init(&readFile, &sendEspNow);
  server.setOnIdleCb(&endWindow);

  while(1) {
    server.loop();
    vTaskDelay(1);
  }
}
