#ifndef SAMPLE_H
#define SAMPLE_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

void sample_task(void *pvParameter);

// this is used to protect access to the file with index sample_file_index
// because mtftp_task and sample_task could both access it at the same time
// eg when a transfer is going on (reading from this file) and
//   sample_task needs to write samples to file

/*
  sample_file_semaph is used to lock access to sample_file_index
  this works fine if (time to transmit sample_file_index) < (time to fill one buffer).
  to fix, probably can make the semaphorewait timeout and write to sample_file_index + 1 instead
*/

extern uint16_t sample_file_index;
extern SemaphoreHandle_t sample_file_semaph;

#endif
