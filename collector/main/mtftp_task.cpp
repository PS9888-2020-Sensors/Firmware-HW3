#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
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

// interval in microseconds
static const uint32_t REPORT_INTERVAL = 1000000;

static MtftpClient client;

enum state {
  STATE_FIND_PEER,    // finding peer to communicate with
  STATE_LOAD_LIST,    // reading file index 0 from server
  STATE_START_READ,   // file read just finished, start reading next file
  STATE_ACTIVE        // transfer in progress
};

struct {
  uint8_t peer_addr[6];
  int8_t buffered_tx;
  enum state state;

  int64_t last_report;
  uint32_t bytes_rx;

  RingbufHandle_t file_entries;
} local_state;

const uint8_t MAC_BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static bool writeFile(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw) {
  const char *TAG = "writeFile";

  ESP_LOGV(TAG, "file_index=%d file_offset=%d btw=%d", file_index, file_offset, btw);
  local_state.bytes_rx += btw;

  if (file_index == 0) {
    // clear the ringbuffer
    if (file_offset == 0) {
      size_t read;
      char *item = (char *) xRingbufferReceiveUpTo(local_state.file_entries, &read, 0, CONFIG_LEN_FILE_LIST * sizeof(file_list_entry_t));
      if (item != NULL) vRingbufferReturnItem(local_state.file_entries, (void *) item);
    }

    size_t free_size = xRingbufferGetCurFreeSize(local_state.file_entries);

    if (free_size > 0) {
      if ((free_size % sizeof(file_list_entry_t)) != 0) {
        ESP_LOGW(TAG, "free_size=%d is not multiple of file_list_entry_t:", free_size);
      }

      // TODO: check whether file is needed before sending to the ringbuffer

      // store up to min(free_size, btw) bytes
      uint16_t write_size = free_size;
      if (write_size > btw) write_size = btw;
      xRingbufferSend(local_state.file_entries, data, write_size, 0);

      return true;
    }

    return true;
  }

  return true;
}

static void onSendEspNowCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  local_state.buffered_tx --;
}

static void onRecvEspNowCb(const uint8_t *mac_addr, const uint8_t *data, int len) {
  const char *TAG = "onRecvEspNowCb";
  ESP_LOGV(TAG, "received packet from " FORMAT_MAC ", len=%d, data[0]=%02x", ARG_MAC(mac_addr), len, (unsigned int) data[0]);

  if (local_state.state == STATE_FIND_PEER) {
    if (len == LEN_SYNC_PACKET && memcmp(data, SYNC_PACKET, LEN_SYNC_PACKET) == 0) {
      ESP_LOGI(TAG, "sync packet received from " FORMAT_MAC, ARG_MAC(mac_addr));

      espnow_add_peer(mac_addr);
      memcpy(local_state.peer_addr, mac_addr, 6);

      local_state.state = STATE_LOAD_LIST;
      client.beginRead(0, 0);
    } else {
      ESP_LOGW(TAG, "received non sync packet");
    }
  } else if (local_state.state == STATE_LOAD_LIST || local_state.state == STATE_ACTIVE) {
    if (memcmp(mac_addr, local_state.peer_addr, 6) == 0) {
      client.onPacketRecv(data, (uint16_t) len);
    } else {
      ESP_LOGD(TAG, "received packet from non peer");
    }
  }

  ESP_LOGV(TAG, "end");
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

  memset(local_state.peer_addr, 0, 6);
  local_state.state = STATE_FIND_PEER;
}

static void transferEnd(void) {
  local_state.state = STATE_START_READ;
}

static void client_loop_task(void *pvParameter) {
  const char *TAG = "transfer";

  while(1) {
    client.loop();

    int64_t time_diff = esp_timer_get_time() - local_state.last_report;
    if (local_state.bytes_rx > 0 && time_diff > REPORT_INTERVAL) {
      ESP_LOGI(TAG, "%d bytes at %.2f kbyte/s", local_state.bytes_rx, (double) local_state.bytes_rx / 1024 / (time_diff / 1000000));
      
      local_state.last_report = esp_timer_get_time();
      local_state.bytes_rx = 0;
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void mtftp_task(void *pvParameter) {
  const char *TAG = "mtftp_task";

  memset(&local_state, 0, sizeof(local_state));
  local_state.file_entries = xRingbufferCreate(CONFIG_LEN_FILE_LIST * sizeof(file_list_entry_t), RINGBUF_TYPE_BYTEBUF);

  esp_now_register_send_cb(onSendEspNowCb);
  esp_now_register_recv_cb(onRecvEspNowCb);

  espnow_add_peer(MAC_BROADCAST);

  client.init(&writeFile, &sendEspNow);
  client.setOnTimeoutCb(&endWindow);
  client.setOnTransferEndCb(&transferEnd);

  xTaskCreate(client_loop_task, "client_loop_task", 2048, NULL, 5, NULL);

  while(1) {
    if (local_state.state == STATE_FIND_PEER) {
      esp_now_send(MAC_BROADCAST, SYNC_PACKET, LEN_SYNC_PACKET);
    } else if (local_state.state == STATE_START_READ) {
      // if files are available in file_entries, start reading the next one
      size_t read;
      file_list_entry_t *entry = (file_list_entry_t *) xRingbufferReceiveUpTo(local_state.file_entries, &read, 0, sizeof(file_list_entry_t));

      if (entry != NULL) {
        client.beginRead(entry->index, 0);
        vRingbufferReturnItem(local_state.file_entries, (void *) entry);
        local_state.state = STATE_ACTIVE;
      } else {
        ESP_LOGI(TAG, "no more files queued");
        endWindow();
        while(1) vTaskDelay(1000);
      }
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}
