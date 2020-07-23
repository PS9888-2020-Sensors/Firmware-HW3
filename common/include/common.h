#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include "esp_err.h"

// TODO: shift this to kconfig
#define DATA_RATE WIFI_PHY_RATE_24M

#define FORMAT_MAC "%02x:%02x:%02x:%02x:%02x:%02x"
#define ARG_MAC(mac) (unsigned int) mac[0], (unsigned int) mac[1], (unsigned int) mac[2], (unsigned int) mac[3], (unsigned int) mac[4], (unsigned int) mac[5]

void nvs_init(void);
void wifi_init(void);
void espnow_init(void);
void sd_init(void);

esp_err_t espnow_add_peer(const uint8_t *peer_addr);

extern const char *SD_MOUNT_POINT;
#define LEN_SYNC_PACKET 8
extern const uint8_t SYNC_PACKET[LEN_SYNC_PACKET];

#endif
