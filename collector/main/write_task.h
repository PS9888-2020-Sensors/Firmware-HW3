#ifndef WRITE_TASK_H
#define WRITE_TASK_

void wait_for_close(void);
bool write_sd(uint8_t addr[], uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw);
void write_task(void *pvParameter);

#endif
