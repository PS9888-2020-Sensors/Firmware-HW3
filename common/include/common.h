#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include "esp_err.h"

// TODO: shift this to kconfig
#define DATA_RATE WIFI_PHY_RATE_24M

// maximum number of packets buffered in esp-now
// wifi alloc failure observed when > 32 are buffered
static const uint8_t MAX_BUFFERED_TX = 8;

#define FORMAT_MAC "%02x:%02x:%02x:%02x:%02x:%02x"
#define ARG_MAC(mac) (unsigned int) mac[0], (unsigned int) mac[1], (unsigned int) mac[2], (unsigned int) mac[3], (unsigned int) mac[4], (unsigned int) mac[5]

void nvs_init(void);
void wifi_init(void);
void espnow_init(void);
void sd_init(void);
void hw_init(void);

void set_led(bool on);

bool get_file_size(uint16_t file_index, uint32_t *size);

esp_err_t espnow_add_peer(const uint8_t *peer_addr);
// two separate functions for setting addr/sending data so that
// caller of sendEspNow dosent need to know the address
void setEspNowTxAddr(uint8_t *addr);
void sendEspNow(const uint8_t *data, uint8_t len);

void sd_activate(void);
void sd_deactivate(void);

extern const char *SD_MOUNT_POINT;
#define LEN_SYNC_PACKET 8
extern const uint8_t SYNC_PACKET[LEN_SYNC_PACKET];

typedef struct __attribute__((__packed__)) {
  uint16_t index;
  uint32_t size;
} file_list_entry_t;

// len("/sdcard/") + str(file_index) + null
// 8 + max 5 + 1
static const uint8_t LEN_MAX_FNAME = 14;

#endif
