#ifndef DUMP_DATA_H
#define DUMP_DATA_H

#include <stdint.h>

void dump_data_init();
void dump_data(uint8_t *mac, uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw);

#endif
