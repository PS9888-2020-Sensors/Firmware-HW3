#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_fat.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "mtftp_client.hpp"

#include "mtftp_task.h"
#include "common.h"

// maximum number of packets buffered in esp-now
// wifi alloc failure observed when > 32 are buffered
static const uint8_t MAX_BUFFERED_TX = 8;

static MtftpClient client;

enum state {
  STATE_FIND_PEER,
  STATE_ACTIVE
};

struct {
  uint8_t peer_addr[6];
  int8_t buffered_tx;
  enum state state;

  int64_t time_transfer_start;
  uint32_t bytes_rx;
} local_state;

const uint8_t MAC_BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static bool writeFile(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw) {
  const char *TAG = "writeFile";

  ESP_LOGV(TAG, "file_index=%d file_offset=%d btw=%d", file_index, file_offset, btw);
  local_state.bytes_rx += btw;

  return true;
}

static void onSendEspNowCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  local_state.buffered_tx --;
}

static void onRecvEspNowCb(const uint8_t *mac_addr, const uint8_t *data, int len) {
  const char *TAG = "onRecvEspNowCb";
  ESP_LOGD(TAG, "received packet from " FORMAT_MAC ", len=%d, data[0]=%02x", ARG_MAC(mac_addr), len, (unsigned int) data[0]);

  if (local_state.state == STATE_FIND_PEER) {
    if (len == LEN_SYNC_PACKET && memcmp(data, SYNC_PACKET, LEN_SYNC_PACKET) == 0) {
      ESP_LOGI(TAG, "sync packet received");

      espnow_add_peer(mac_addr);
      memcpy(local_state.peer_addr, mac_addr, 6);

      local_state.state = STATE_ACTIVE;

      local_state.time_transfer_start = esp_timer_get_time();
      local_state.bytes_rx = 0;

      client.beginRead(1, 0);
    } else {
      ESP_LOGW(TAG, "received non sync packet");
    }
  } else if (local_state.state == STATE_ACTIVE) {
    if (memcmp(mac_addr, local_state.peer_addr, 6) == 0) {
      client.onPacketRecv(data, (uint16_t) len);
    } else {
      ESP_LOGD(TAG, "received packet from non peer");
    }
  }

  ESP_LOGD(TAG, "end");
}

static void sendEspNow(const uint8_t *data, uint8_t len) {
  while (local_state.buffered_tx > MAX_BUFFERED_TX) {
    vTaskDelay(1);
  }

  ESP_ERROR_CHECK(esp_now_send((const uint8_t *) local_state.peer_addr, data, len));
  local_state.buffered_tx ++;
}

static void endWindow(void) {
  const char *TAG = "endWindow";

  esp_now_del_peer(local_state.peer_addr);
  ESP_LOGI(TAG, "ending window");

  ESP_LOGI(TAG, "took %llu to transfer %d bytes", esp_timer_get_time() - local_state.time_transfer_start, local_state.bytes_rx);

  memset(local_state.peer_addr, 0, 6);
  local_state.state = STATE_FIND_PEER;
}

void mtftp_task(void *pvParameter) {
  memset(&local_state, 0, sizeof(local_state));

  esp_now_register_send_cb(onSendEspNowCb);
  esp_now_register_recv_cb(onRecvEspNowCb);

  espnow_add_peer(MAC_BROADCAST);

  client.init(&writeFile, &sendEspNow);
  client.setOnIdleCb(&endWindow);

  while(1) {
    client.loop();
    
    if (local_state.state == STATE_FIND_PEER) {
      esp_now_send(MAC_BROADCAST, SYNC_PACKET, LEN_SYNC_PACKET);
      vTaskDelay(10000 / portTICK_PERIOD_MS);
    } else {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}
