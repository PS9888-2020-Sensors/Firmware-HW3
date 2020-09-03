#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"

const spi_host_device_t SENSOR_SPI_HOST = SPI2_HOST;
const uint32_t SENSOR_SPI_SPEED = 1000000;

const gpio_num_t GPIO_SPI_SCLK = GPIO_NUM_27;
const gpio_num_t GPIO_SPI_MISO = GPIO_NUM_26;
const gpio_num_t GPIO_SPI_MOSI = GPIO_NUM_25;
const gpio_num_t GPIO_SPI_CS_ADC = GPIO_NUM_23;
const gpio_num_t GPIO_SPI_CS_BME = GPIO_NUM_22;

const gpio_num_t GPIO_POWER_SEL = GPIO_NUM_18;

const gpio_num_t GPIO_LED = GPIO_NUM_5;
const gpio_num_t GPIO_BTN_USER = GPIO_NUM_39;
