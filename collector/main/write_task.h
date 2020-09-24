#ifndef WRITE_TASK_H
#define WRITE_TASK_

bool write_sd(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw);
void write_task(void *pvParameter);

#endif