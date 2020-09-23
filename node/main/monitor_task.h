#ifndef MONITOR_TASK_H
#define MONITOR_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

typedef enum {
  EVT_TIME_SYNCED,
  EVT_COMMS_START,
  EVT_COMMS_END,
  EVT_SHUTDOWN_WRITE_DONE
} Event_t;

extern QueueHandle_t evt_queue;
extern bool shutdown;
void monitor_task(void *pvParameter);

#endif