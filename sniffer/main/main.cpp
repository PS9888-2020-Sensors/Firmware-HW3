#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "common.h"
#include "mtftp.h"

extern "C" {
  void app_main();
}

#define CATEGORY_VENDOR_SPECIFIC 127
#define VENDOR_SPECIFIC_ELEMENT 221
#define TYPE_ESP_NOW 4
#define LEN_FCS 4

const char ESP_NOW_ORG_ID[3] = { 0x18, 0xfe, 0x34 };

// https://github.com/SHA2017-badge/bpp/blob/master/esp32-recv/main/bpp_sniffer.c
typedef struct {
  uint8_t mac[6];
} __attribute__((packed)) MacAddr;

typedef struct {
  int16_t fctl;
  int16_t duration;
  MacAddr da;
  MacAddr sa;
  MacAddr bssid;
  int16_t seqctl;
  unsigned char payload[];
} __attribute__((packed)) WifiMgmtHdr;

// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html
typedef struct {
  uint8_t category;
  uint8_t org_id1[3];
  uint8_t random[4];
  uint8_t element_id;
  // length - len(element_id) - len(org_id2) - len(type) - len(version) = len(ESP-NOW payload)
  uint8_t length;
  uint8_t org_id2[3];
  uint8_t type;
  uint8_t version;
  uint8_t payload[];
} __attribute__((packed)) ActionFrame;

const char *TAG = "sniffer";
void promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;

  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *) buf;

  WifiMgmtHdr *wh = (WifiMgmtHdr *) pkt->payload;
  ActionFrame *af = (ActionFrame *) wh->payload;

  // check that this packet is a ESP-NOW packet
  if (
    af->category != CATEGORY_VENDOR_SPECIFIC ||
    memcmp(af->org_id1, ESP_NOW_ORG_ID, 3) != 0 ||
    memcmp(af->org_id2, ESP_NOW_ORG_ID, 3) != 0 ||
    af->element_id != VENDOR_SPECIFIC_ELEMENT ||
    af->type != TYPE_ESP_NOW
  ) return;

  const uint16_t LEN_OUTPUT = 512;
  char output[LEN_OUTPUT];
  char tmp[16];

  // ESP_LOG_BUFFER_HEX_LEVEL(TAG, wh->payload, len, ESP_LOG_INFO);

  switch (af->payload[0]) {
    case 0:
      if (memcmp(af->payload, SYNC_PACKET, 6) == 0) {
        strcpy(output, "\033[36msync packet");
      } else {
        strcpy(output, "\033[31mmalformed sync packet");
      }
      break;
    case TYPE_READ_REQUEST:
    {
      packet_rrq_t *pkt_rrq = (packet_rrq_t *) af->payload;
      snprintf(output, LEN_OUTPUT, "\033[32mrrq file_index=%d file_offset=%d window_size=%d", pkt_rrq->file_index, pkt_rrq->file_offset, pkt_rrq->window_size);
      break;
    }
    case TYPE_DATA:
      break;
    case TYPE_RETRANSMIT:
    {
      packet_rtx_t *pkt_rtx = (packet_rtx_t *) af->payload;
      snprintf(output, LEN_OUTPUT, "\033[34mrtx of %d blocks: ", pkt_rtx->num_elements);
      for (uint8_t i = 0; i < pkt_rtx->num_elements; i++) {
        snprintf(tmp, 16, "%d ", pkt_rtx->block_nos[i]);
        strcat(output, tmp);
      }
      break;
    }
    case TYPE_ACK:
    {
      packet_ack_t *pkt_ack = (packet_ack_t *) af->payload;
      snprintf(output, LEN_OUTPUT, "\033[35mack of block %d", pkt_ack->block_no);
      break;
    }
    case TYPE_ERR:
    {
      packet_err_t *pkt_err = (packet_err_t *) af->payload;
      snprintf(output, LEN_OUTPUT, "error: %s", err_types_str[pkt_err->err]);
      break;
    }
    default:
      ESP_LOGW(TAG, "unknown ESP-NOW packet");
      break;
  }

  printf(
    "[%.3f] src=" FORMAT_MAC " dst=" FORMAT_MAC " %s\n",
    pkt->rx_ctrl.timestamp / 1000000.0,
    ARG_MAC(wh->sa.mac),
    ARG_MAC(wh->da.mac),
    output
  );
}

void app_main(void) {
  nvs_init();
  wifi_init();

  ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
  ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(&promiscuous_cb));
}
