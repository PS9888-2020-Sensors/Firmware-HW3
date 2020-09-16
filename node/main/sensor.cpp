#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "board.h"

#define BME280_FLOAT_ENABLE
#include "bme280/bme280.h"

static const char *TAG = "sensor_task";

void user_delay_us(uint32_t period, void *intf_ptr)
{
  // this is a busy loop, but delay is only used in init/reset
  // so should be acceptable
  ets_delay_us(period);
}

int8_t user_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
  /* Return 0 for Success, non-zero for failure */
  int8_t ret = 1;

  spi_device_handle_t *spi = (spi_device_handle_t *) intf_ptr;

  gpio_set_level(GPIO_SPI_CS_BME, 0);

  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8;
  t.tx_buffer = &reg_addr;

  if (spi_device_polling_transmit(*spi, &t) == ESP_OK) {
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.rx_buffer = reg_data;

    if (spi_device_polling_transmit(*spi, &t) == ESP_OK) {
      ret = 0;
    }
  }

  gpio_set_level(GPIO_SPI_CS_BME, 1);

  return ret;
}

int8_t user_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
  /* Return 0 for Success, non-zero for failure */
  int8_t ret = 1;

  spi_device_handle_t *spi = (spi_device_handle_t *) intf_ptr;

  gpio_set_level(GPIO_SPI_CS_BME, 0);

  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8;
  t.tx_buffer = &reg_addr;

  if (spi_device_polling_transmit(*spi, &t) == ESP_OK) {
    t.length = len * 8;
    t.tx_buffer = reg_data;

    if (spi_device_polling_transmit(*spi, &t) == ESP_OK) {
      ret = 0;
    }
  }

  gpio_set_level(GPIO_SPI_CS_BME, 1);

  return ret;
}

static spi_device_handle_t spi_init(void) {
  spi_device_handle_t spi;

  spi_bus_config_t buscfg;
  memset(&buscfg, 0, sizeof(spi_bus_config_t));
  buscfg.mosi_io_num = GPIO_SPI_MOSI;
  buscfg.miso_io_num = GPIO_SPI_MISO;
  buscfg.sclk_io_num = GPIO_SPI_SCLK;

  spi_device_interface_config_t devcfg;
  memset(&devcfg, 0, sizeof(spi_device_interface_config_t));
  devcfg.mode = 0,
  devcfg.spics_io_num = -1;
  devcfg.clock_speed_hz = SENSOR_SPI_SPEED;
  devcfg.queue_size = 4;

  ESP_ERROR_CHECK(spi_bus_initialize(SENSOR_SPI_HOST, &buscfg, 0));
  ESP_ERROR_CHECK(spi_bus_add_device(SENSOR_SPI_HOST, &devcfg, &spi));

  // we manually assert/negate CS rather than using the SPI controller
  gpio_set_direction(GPIO_SPI_CS_BME, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_SPI_CS_BME, 1);

  return spi;
}

struct bme280_dev dev;
spi_device_handle_t spi;

void sensor_init(void) {
  spi = spi_init();

  dev.intf_ptr = &spi;
  dev.intf = BME280_SPI_INTF;
  dev.read = user_spi_read;
  dev.write = user_spi_write;
  dev.delay_us = user_delay_us;

  if (bme280_init(&dev) != BME280_OK) {
    ESP_LOGW(TAG, "Failed to initialise BME280!");
    abort();
  }

  dev.settings.osr_h = BME280_OVERSAMPLING_1X;
  dev.settings.osr_p = BME280_OVERSAMPLING_1X;
  dev.settings.osr_t = BME280_OVERSAMPLING_1X;
  dev.settings.filter = BME280_FILTER_COEFF_2;

  uint8_t settings_sel = BME280_OSR_PRESS_SEL | BME280_OSR_TEMP_SEL | BME280_OSR_HUM_SEL | BME280_FILTER_SEL;

  bme280_set_sensor_settings(settings_sel, &dev);
  bme280_set_sensor_mode(BME280_FORCED_MODE, &dev);

  ESP_LOGI(TAG, "min delay = %dms", bme280_cal_meas_delay(&(dev.settings)));
}

uint32_t sensor_read(void) {
  struct bme280_data comp_data;
  bme280_get_sensor_data(BME280_ALL, &comp_data, &dev);

  ESP_LOGV(TAG, "bme280: temp=%f pressure=%f humidity=%f",comp_data.temperature, comp_data.pressure, comp_data.humidity);

  // convert the float into a 24 bit int (very crudely)
  return ((uint32_t) (comp_data.pressure * 100)) & 0xFFFFFF;
}

void sensor_start_read(void) {
  bme280_set_sensor_mode(BME280_FORCED_MODE, &dev);
}