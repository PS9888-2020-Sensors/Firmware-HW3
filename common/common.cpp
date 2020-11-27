#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_private/wifi.h"
#include "esp_now.h"
#include "driver/gpio.h"

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

#include "board.h"

const char *SD_MOUNT_POINT = "/sdcard";
const uint8_t SYNC_PACKET[LEN_SYNC_PACKET] = { 0x00, 0xf5, 0x3a, 0x72, 0x89, 0x13, 0x57, 0xa5 };

void nvs_init(void) {
  esp_err_t ret = nvs_flash_init();

  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  ESP_ERROR_CHECK(ret);
}

void wifi_init(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  // disabled so we can change wifi rate later
  // https://github.com/espressif/esp-idf/blob/release/v4.2/components/esp_wifi/include/esp_private/wifi.h#L199
  cfg.ampdu_tx_enable = 0;

  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
  ESP_ERROR_CHECK(esp_wifi_internal_set_fix_rate(ESP_IF_WIFI_STA, true, DATA_RATE));
}

void hw_init(void) {
  gpio_set_direction(GPIO_POWER_SEL, GPIO_MODE_OUTPUT);
  gpio_set_direction(GPIO_LED, GPIO_MODE_OUTPUT);
  gpio_set_direction(GPIO_BTN_USER, GPIO_MODE_INPUT);

  set_led(0);
}

void set_led(bool on) {
  gpio_set_level(GPIO_LED, on);
}

int get_btn_user(void) {
  return gpio_get_level(GPIO_BTN_USER);
}

SemaphoreHandle_t can_tx;
uint8_t *espnow_tx_addr = NULL;

void setEspNowTxAddr(uint8_t *addr) {
  espnow_tx_addr = addr;
}

void sendEspNow(const uint8_t *data, uint8_t len) {
  #ifdef CONFIG_SIMULATE_PACKET_LOSS
    if (esp_random() % CONFIG_PACKET_LOSS_MOD == 0) {
      ESP_LOGI("send", "drop data[0]=%02x", data[0]);
      return;
    }
  #endif

  while (xSemaphoreTake(can_tx, 50 / portTICK_PERIOD_MS) != pdTRUE) {
    ESP_LOGI("wait", "waiting");
  }
  ESP_ERROR_CHECK(esp_now_send((const uint8_t *) espnow_tx_addr, data, len));
}

static void onSendEspNowCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  xSemaphoreGive(can_tx);
}

void espnow_init(void) {
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_send_cb(onSendEspNowCb));

  can_tx = xSemaphoreCreateCounting(MAX_BUFFERED_TX, MAX_BUFFERED_TX);
  assert(can_tx != NULL);

  #ifdef CONFIG_SIMULATE_PACKET_LOSS
    const char *TAG = "espnow_init";
    ESP_LOGW(TAG, "will randomly drop outgoing packets because SIMULATE_PACKET_LOSS is set");
  #endif
}

void sd_init(void) {
  const char *TAG = "sd_init";

  esp_vfs_fat_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 5,
    .allocation_unit_size = 16 * 1024
  };

  aux_activate();

  vTaskDelay(500 / portTICK_RATE_MS);

  sdmmc_card_t* card;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

  gpio_set_pull_mode(GPIO_NUM_15, GPIO_PULLUP_ONLY);   // CMD
  gpio_set_pull_mode(GPIO_NUM_2,  GPIO_PULLUP_ONLY);   // D0
  gpio_set_pull_mode(GPIO_NUM_4,  GPIO_PULLUP_ONLY);   // D1
  gpio_set_pull_mode(GPIO_NUM_12, GPIO_PULLUP_ONLY);   // D2
  gpio_set_pull_mode(GPIO_NUM_13, GPIO_PULLUP_ONLY);   // D3

  esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "failed to mount filesystem");
    }

    ESP_ERROR_CHECK(ret);
  }

  sdmmc_card_print_info(stdout, card);
}

bool conv_strtoul(char *str, uint16_t *num) {
  errno = 0;
  uint16_t val = strtoul(str, NULL, 10);
  if (errno != 0) return false;

  if (num != NULL) *num = val;

  return true;
}

bool get_file_size(char *fname, uint32_t *size) {
  const char *TAG = "get_file_size";

  FILE *fp = fopen(fname, "r");

  if (fp == NULL) {
    return false;
  }

  if (fseeko(fp, 0, SEEK_END) != 0) {
    ESP_LOGE(TAG, "fseek %s failed", fname);
    return false;
  }

  *size = ftello(fp);
  fclose(fp);

  ESP_LOGI(TAG, "file=%s has size=%d", fname, *size);

  return true;
}

bool get_file_size(uint16_t file_index, uint32_t *size) {
  char fname[LEN_MAX_FNAME];
  snprintf(fname, LEN_MAX_FNAME, "%s/%d", SD_MOUNT_POINT, file_index);

  return get_file_size(fname, size);
}

bool get_file_size(uint8_t addr[], uint16_t file_index, uint32_t *size) {
  char fname[LEN_MAX_FNAME];
  get_addr_id_path(addr, file_index, fname);

  return get_file_size(fname, size);
}

void get_addr_id_path(uint8_t addr[], uint16_t file_index, char *out) {
  snprintf(out, LEN_MAX_FNAME, "%s/%02X%02X%02X%02X%02X%02X-%d", SD_MOUNT_POINT, ARG_MAC(addr), file_index);
}

uint64_t get_time(void) {
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  return (uint64_t) tv_now.tv_sec * 1000000L + (uint64_t) tv_now.tv_usec;
}

esp_err_t espnow_add_peer(const uint8_t *peer_addr) {
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = CONFIG_WIFI_CHANNEL;
  peer.ifidx = ESP_IF_WIFI_STA;
  peer.encrypt = false;
  memcpy(peer.peer_addr, peer_addr, 6);
  return esp_now_add_peer(&peer);
}

void aux_activate(void) {
  gpio_set_level(GPIO_POWER_SEL, 0);
}

void aux_deactivate(void) {
  gpio_set_level(GPIO_POWER_SEL, 1);
}
