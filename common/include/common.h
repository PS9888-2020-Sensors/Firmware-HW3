#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include "esp_err.h"

// TODO: shift this to kconfig
#define DATA_RATE WIFI_PHY_RATE_24M

void nvs_init(void);
void wifi_init(void);
void espnow_init(void);
void sd_init(void);

esp_err_t espnow_add_peer(const uint8_t *peer_addr);

extern const char *SD_MOUNT_POINT;
#define LEN_SYNC_PACKET 8
extern const uint8_t SYNC_PACKET[LEN_SYNC_PACKET];

#endif
