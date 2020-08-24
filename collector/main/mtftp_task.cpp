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

// interval in microseconds
static const uint32_t REPORT_INTERVAL = 1000000;

static MtftpClient client;

enum state {
  STATE_FIND_PEER,    // finding peer to communicate with
  STATE_LOAD_LIST,    // reading file index 0 from server
  STATE_WAIT_WRITES,  // wait for all current writes to finish
  STATE_START_READ,   // start reading next file
  STATE_ACTIVE        // transfer in progress
};

struct {
  uint8_t peer_addr[6];
  enum state state;

  int64_t last_report;
  uint32_t bytes_rx;

  uint16_t file_index;
  uint16_t target_file_index;
  FILE *fp;

  // buffers data to be written to file
  // stores data of format write_buffer_header_t
  RingbufHandle_t write_buffer;
  // stores list of file_list_entry_t
  // TODO: why isn't this a queue?
  RingbufHandle_t file_entries;
} local_state;

const uint8_t MAC_BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static bool writeFile(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw) {
  const char *TAG = "writeFile";

  ESP_LOGD(TAG, "file_index=%d file_offset=%d btw=%d", file_index, file_offset, btw);
  local_state.bytes_rx += btw;

  if (file_index == 0) {
    // file offset 0 is handled specially:
    // this is an array of file_list_entry_t that is available on the server
    // this chunk of code stores files that it wants in a ringbuffer

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

  if (local_state.file_index != 0 && file_index != local_state.file_index) {
    ESP_LOGE(TAG, "target file_index of %d differs from current open %d", file_index, local_state.file_index);
    return false;
  }

  local_state.target_file_index = file_index;

  if (xRingbufferSend(local_state.write_buffer, &file_offset, sizeof(uint32_t), 0) != pdTRUE) {
    ESP_LOGW(TAG, "failed to send offset to ringbuffer");
    return false;
  };

  if (xRingbufferSend(local_state.write_buffer, data, btw, 0) != pdTRUE) {
    ESP_LOGW(TAG, "failed to send data to ringbuffer");
    return false;
  };

  return true;
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

static void close_fp(void) {
  if (local_state.file_index != 0) {
    ESP_LOGI("close_fp", "fclose %d", local_state.file_index);
    fclose(local_state.fp);

    local_state.file_index = 0;
  }
}

static void endPeered(void) {
  const char *TAG = "endPeered";

  esp_now_del_peer(local_state.peer_addr);
  ESP_LOGI(TAG, "ending peered communication");

  memset(local_state.peer_addr, 0, 6);
  local_state.state = STATE_FIND_PEER;

  close_fp();
}

static void transferEnd(void) {
  local_state.state = STATE_WAIT_WRITES;
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

static void write_file_task(void *pvParameter) {
  const char *TAG = "write_file_task";

  while(1) {
    char file_offset[4];
    size_t size1 = 0, size2 = 0;
    char *item1, *item2;

    if (xRingbufferReceiveSplit(
      local_state.write_buffer,
      (void **) &item1,
      (void **) &item2,
      &size1,
      &size2,
      100 / portTICK_PERIOD_MS
    ) != pdTRUE) {
      continue;
    }

    if (item1 == NULL) continue;

    // this item should be 4 bytes (file offset)
    // combine item1 and item2 if necessary

    if ((size1 + size2) != 4) {
      ESP_LOGW(TAG, "expected 4 byte file_offset but got %d bytes", size1 + size2);
      continue;
    }

    memcpy(file_offset, item1, size1);
    vRingbufferReturnItem(local_state.write_buffer, (void *) item1);
    if (size1 < 4) {
      if (item2 == NULL) {
        ESP_LOGW(TAG, "could not retrieve full file_offset");
        continue;
      }
      memcpy(file_offset + size1, item2, size2);
      vRingbufferReturnItem(local_state.write_buffer, (void *) item2);
    }

    // open the correct file pointer if not already open
    if (local_state.file_index != local_state.target_file_index) {
      if (local_state.file_index != 0) {
        ESP_LOGI(TAG, "fclose %d", local_state.file_index);
        fclose(local_state.fp);
      }

      char fname[LEN_MAX_FNAME];

      snprintf(fname, LEN_MAX_FNAME, "%s/%d", SD_MOUNT_POINT, local_state.target_file_index);

      local_state.fp = fopen(fname, "a");
      if (local_state.fp == NULL) {
        ESP_LOGE(TAG, "fopen %s failed", fname);
        abort();
      }

      if (setvbuf(local_state.fp, NULL, _IOFBF, CONFIG_WRITE_BUF_SIZE) != 0) {
        ESP_LOGE(TAG, "setvbuf failed");
        abort();
      }

      ESP_LOGI(TAG, "fopen %d", local_state.target_file_index);

      local_state.file_index = local_state.target_file_index;
    }

    if (fseek(local_state.fp, *((uint32_t *) file_offset), SEEK_SET) != 0) {
      ESP_LOGE(TAG, "fseek of %d to %d failed", local_state.target_file_index, *((uint32_t *) file_offset));
      abort();
    }

    // retrieve actual file data
    if (xRingbufferReceiveSplit(
      local_state.write_buffer,
      (void **) &item1,
      (void **) &item2,
      &size1,
      &size2,
      1
    ) != pdTRUE) {
      ESP_LOGW(TAG, "data not available!");
      continue;
    }

    if (item1 == NULL) {
      ESP_LOGW(TAG, "data not available!");
      continue;
    }

    size_t bw = fwrite(item1, 1, size1, local_state.fp);
    vRingbufferReturnItem(local_state.write_buffer, (void *) item1);
    if (bw != size1) {
      ESP_LOGE(TAG, "write failed, only %d bytes written (!= %d)", bw, size1);
    }

    ESP_LOGD(TAG, "wrote %d bytes at %d", bw, *((uint32_t *) file_offset));

    if (item2 == NULL) continue;

    bw = fwrite(item2, 1, size2, local_state.fp);
    vRingbufferReturnItem(local_state.write_buffer, (void *) item2);
    if (bw != size2) {
      ESP_LOGE(TAG, "write failed, only %d bytes written (!= %d)", bw, size2);
    }

    ESP_LOGD(TAG, "wrote %d bytes", bw);
  }
}

void mtftp_task(void *pvParameter) {
  const char *TAG = "mtftp_task";

  memset(&local_state, 0, sizeof(local_state));
  // allocate enough space to hold the entire buffer and file_offset
  // each item also requires 8 bytes for a header, so allocate space for that too
  local_state.write_buffer = xRingbufferCreate((CONFIG_LEN_MTFTP_BUFFER * CONFIG_LEN_BLOCK) + sizeof(uint32_t) + (8 * 2), RINGBUF_TYPE_ALLOWSPLIT);
  local_state.file_entries = xRingbufferCreate(CONFIG_LEN_FILE_LIST * sizeof(file_list_entry_t), RINGBUF_TYPE_BYTEBUF);

  assert(local_state.write_buffer != NULL);
  assert(local_state.file_entries != NULL);

  setEspNowTxAddr(local_state.peer_addr);
  esp_now_register_recv_cb(onRecvEspNowCb);

  espnow_add_peer(MAC_BROADCAST);

  client.init(&writeFile, &sendEspNow);
  client.setOnTimeoutCb(&endPeered);
  client.setOnTransferEndCb(&transferEnd);

  xTaskCreate(client_loop_task, "client_loop_task", 2048, NULL, 5, NULL);
  xTaskCreate(write_file_task, "write_file_task", 2048, NULL, 6, NULL);

  while(1) {
    if (local_state.state == STATE_FIND_PEER) {
      esp_now_send(MAC_BROADCAST, SYNC_PACKET, LEN_SYNC_PACKET);

      ESP_LOGD(TAG, "broadcasting sync");
      vTaskDelay(100 / portTICK_PERIOD_MS);
    } else if (local_state.state == STATE_WAIT_WRITES) {
      UBaseType_t items_waiting;
      vRingbufferGetInfo(local_state.write_buffer, NULL, NULL, NULL, NULL, &items_waiting);

      if (items_waiting == 0) {
        close_fp();
        local_state.state = STATE_START_READ;
      }
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
        endPeered();
        while(1) vTaskDelay(1000);
      }
    } else {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}
