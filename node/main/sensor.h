#ifndef SENSOR_H
#define SENSOR_H

#define TYPE_SENSOR_READING double

void sensor_init(void);
TYPE_SENSOR_READING sensor_read(void);
void sensor_start_read(void);

void test(void);

#endif
