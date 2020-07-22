#include "common.h"

#include <string.h>

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_private/wifi.h"
#include "esp_now.h"

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

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


void espnow_init(void) {
  ESP_ERROR_CHECK(esp_now_init());
  // ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
  // ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = CONFIG_WIFI_CHANNEL;
  peer.ifidx = ESP_IF_WIFI_STA;
  peer.encrypt = false;

  ESP_ERROR_CHECK(esp_now_add_peer(&peer));
}

void sd_init(void) {
  const char *TAG = "sd_init";

  esp_vfs_fat_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 5,
    .allocation_unit_size = 16 * 1024
  };

  sdmmc_card_t* card;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
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

esp_err_t espnow_add_peer(const uint8_t *peer_addr) {
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = CONFIG_WIFI_CHANNEL;
  peer.ifidx = ESP_IF_WIFI_STA;
  peer.encrypt = false;
  memcpy(peer.peer_addr, peer_addr, 6);
  return esp_now_add_peer(&peer);
}
