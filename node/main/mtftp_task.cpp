#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_vfs_fat.h"
#include "esp_now.h"
#include "esp_log.h"

#include "mtftp_server.hpp"
#include "sample_task.h"
#include "monitor_task.h"

#include "mtftp_task.h"
#include "common.h"

#include "sdkconfig.h"

// interval in ms
static const uint32_t REPORT_INTERVAL = 1000;

static MtftpServer server;

enum state {
  STATE_WAIT_PEER,
  STATE_ACTIVE
};

struct {
  uint8_t peer_addr[6];
  enum state state;
  int64_t time_last_packet;

  uint16_t file_index;
  FILE *fp;

  file_list_entry_t *file_list;
  uint16_t len_file_list;
} local_state;

static uint16_t countFiles(void) {
  const char *TAG = "countFiles";

  DIR *d = opendir(SD_MOUNT_POINT);

  if (d == NULL) return 0;

  struct dirent *dir;
  uint16_t count = 0;

  while ((dir = readdir(d)) != NULL) {
    ESP_LOGD(TAG, "file=%s, type=%d", dir->d_name, dir->d_type);

    if (!conv_strtoul(dir->d_name, NULL)) continue;

    if (dir->d_type == DT_REG) count++;
  }

  closedir(d);

  return count;
}

static uint16_t buildFileList(file_list_entry_t entries[]) {
  const char *TAG = "buildFileList";

  DIR *d = opendir(SD_MOUNT_POINT);

  if (d == NULL) return 0;

  struct dirent *dir;
  uint16_t count = 0;

  while ((dir = readdir(d)) != NULL) {
    if (dir->d_type != DT_REG) continue;

    if (!conv_strtoul(dir->d_name, &(entries[count].index))) continue;

    if (entries[count].index == sample_file_index) {
      xSemaphoreTake(sample_file_semaph, portMAX_DELAY);
    }

    if (!get_file_size(entries[count].index, &(entries[count].size))) {
      if (entries[count].index == sample_file_index) {
        xSemaphoreGive(sample_file_semaph);
      }
      continue;
    }

    if (entries[count].index == sample_file_index) {
      xSemaphoreGive(sample_file_semaph);
    }

    ESP_LOGD(TAG, "filename=%s size=%d", dir->d_name, entries[count].size);
    count++;
  }

  closedir(d);

  return count;
}

static bool readFileList(uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br) {
  const char *TAG = "readFileList";

  // reload file list if offset is 0
  if (file_offset == 0) {
    uint16_t num_files = countFiles();

    if (local_state.file_list != NULL) {
      free(local_state.file_list);
    }

    local_state.len_file_list = num_files *  sizeof(file_list_entry_t);
    local_state.file_list = (file_list_entry_t *) malloc(local_state.len_file_list);

    if (local_state.file_list == NULL) {
      ESP_LOGE(TAG, "failed to malloc");
      return false;
    }

    if (num_files != buildFileList(local_state.file_list)) {
      ESP_LOGW(TAG, "buildFileList got different number of files from countFiles!");
    }
  }

  if (file_offset >= local_state.len_file_list) {
    *br = 0;
    return true;
  } else if ((file_offset + btr) > local_state.len_file_list) {
    *br = local_state.len_file_list - file_offset;
  } else {
    *br = btr;
  }

  memcpy(data, local_state.file_list + file_offset, *br);

  return true;
}

static bool readFile(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br) {
  const char *TAG = "readFile";

  ESP_LOGD(TAG, "readFile of %d at offset=%lu btr=%d", file_index, (unsigned long) file_offset, btr);

  if (file_index == 0) {
    return readFileList(file_offset, data, btr, br);
  }

  if (local_state.file_index != file_index) {
    if (local_state.file_index != 0) {
      ESP_LOGI(TAG, "fclose %d", local_state.file_index);
      fclose(local_state.fp);

      if (local_state.file_index == sample_file_index) {
        xSemaphoreGive(sample_file_semaph);
      }
    }

    if (local_state.file_index == sample_file_index) {
      xSemaphoreTake(sample_file_semaph, portMAX_DELAY);
    }

    char fname[LEN_MAX_FNAME];

    snprintf(fname, LEN_MAX_FNAME, "%s/%d", SD_MOUNT_POINT, file_index);

    local_state.fp = fopen(fname, "r");
    if (local_state.fp == NULL) {
      ESP_LOGE(TAG, "fopen %s failed", fname);
      return false;
    }

    if (setvbuf(local_state.fp, NULL, _IOFBF, CONFIG_READ_BUF_SIZE) != 0) {
      ESP_LOGE(TAG, "setvbuf failed");
      return false;
    }

    ESP_LOGI(TAG, "fopen %d", file_index);

    local_state.file_index = file_index;
  }

  if (fseek(local_state.fp, file_offset, SEEK_SET) != 0) {
    ESP_LOGE(TAG, "fseek of %d to %d failed", file_index, file_offset);
    return false;
  }

  *br = fread(data, 1, btr, local_state.fp);

  return true;
}

static void onRecvEspNowCb(const uint8_t *mac_addr, const uint8_t *data, int len) {
  const char *TAG = "onRecvEspNowCb";
  static bool received_non_sync = false;
  ESP_LOGD(TAG, "received packet from " FORMAT_MAC ", len=%d, data[0]=%02x", ARG_MAC(mac_addr), len, (unsigned int) data[0]);

  if (local_state.state == STATE_WAIT_PEER || !received_non_sync) {
    // if !received_non_sync, collector likely did not receive the SYNC reply
    // accept more SYNC packets until the collector stops sending SYNCs
    if (len == LEN_SYNC_PACKET && memcmp(data, SYNC_PACKET, LEN_SYNC_PACKET) == 0) {
      ESP_LOGI(TAG, "sync packet received from " FORMAT_MAC, ARG_MAC(mac_addr));

      if (memcmp(mac_addr, local_state.peer_addr, 6) != 0) {
        espnow_add_peer(mac_addr);
      }

      // send back the same SYNC packet to the collector as ACK
      memcpy(local_state.peer_addr, mac_addr, 6);
      sendEspNow(data, len);

      local_state.state = STATE_ACTIVE;

      local_state.time_last_packet = esp_timer_get_time();
      received_non_sync = false;

      Event_t evt = EVT_COMMS_START;
      xQueueSend(evt_queue, &evt, 0);
      return;
    }
  }

  if (local_state.state == STATE_ACTIVE) {
    if (memcmp(mac_addr, local_state.peer_addr, 6) == 0) {
      received_non_sync = true;
      server.onPacketRecv(data, (uint16_t) len);

      if (!server.isIdle()) local_state.time_last_packet = esp_timer_get_time();
    } else {
      ESP_LOGD(TAG, "received packet from non peer");
    }
  }
}

static void endPeered(void) {
  const char *TAG = "endPeered";
  Event_t evt = EVT_COMMS_END;
  xQueueSend(evt_queue, &evt, 0);

  esp_now_del_peer(local_state.peer_addr);
  ESP_LOGI(TAG, "ending peered communication");

  memset(local_state.peer_addr, 0, 6);
  local_state.state = STATE_WAIT_PEER;

  if (local_state.file_index != 0) {
    ESP_LOGI(TAG, "fclose %d", local_state.file_index);
    fclose(local_state.fp);

    if (local_state.file_index == sample_file_index) {
      xSemaphoreGive(sample_file_semaph);
    }

    local_state.file_index = 0;
  }
}

static void rate_logging_task(void *pvParameter) {
  const char *TAG = "transfer";

  while(1) {
    if (packet_send_count > 0 || packet_fail_count > 0) {
      ESP_LOGI(TAG, "%d packets (~%d kbyte) sent, %d lost", packet_send_count, packet_send_count * 247 / 1024, packet_fail_count);
      packet_send_count = 0;
      packet_fail_count = 0;
    }
    vTaskDelay(REPORT_INTERVAL / portTICK_PERIOD_MS);
  }
}

void mtftp_task(void *pvParameter) {
  const char *TAG = "mtftp_task";
  memset(&local_state, 0, sizeof(local_state));

  setEspNowTxAddr(local_state.peer_addr);
  esp_now_register_recv_cb(onRecvEspNowCb);

  xTaskCreate(rate_logging_task, "rate_logging_task", 2048, NULL, 3, NULL);

  server.init(&readFile, &sendEspNow);
  server.setOnTimeoutCb(&endPeered);

  while(1) {
    server.loop();
    if (server.isIdle()) {
      if (local_state.state == STATE_ACTIVE && (esp_timer_get_time() - local_state.time_last_packet) > CONFIG_TIMEOUT) {
        ESP_LOGI(TAG, "timeout in idle");
        endPeered();
      }

      vTaskDelay(1);
    }
  }
}
