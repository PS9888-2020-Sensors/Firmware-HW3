#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include <sys/time.h>

#include "board.h"
#include "common.h"

#include "sample_task.h"

static QueueHandle_t uart_queue;
static const int UART_NUM = UART_NUM_2;
static const int LEN_BUF_TX = 256;
static const int LEN_BUF_RX = 256;
static const int LEN_BUF_READ = 128;
static const int QUEUE_LEN = 8;

static const int ESP_INTR_FLAG_DEFAULT = 0;

// system time where next_time was configured
static int64_t next_time_arrival = -10000000;
// absolute time, valid on next pps pulse
static struct timeval next_time;

static void IRAM_ATTR pps_isr_handler(void *arg) {
  // sanity check that the value of next_time was set within the last second
  if ((esp_timer_get_time() - next_time_arrival) > 950000) return;

  settimeofday(&next_time, NULL);

  BaseType_t xHigherPriorityTaskWoken;
  xSemaphoreGiveFromISR(time_acquired_semaph, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

static void pps_init(void) {
  ESP_ERROR_CHECK(gpio_set_direction(GPIO_GPS_PPS, GPIO_MODE_INPUT));
  ESP_ERROR_CHECK(gpio_intr_enable(GPIO_GPS_PPS));
  ESP_ERROR_CHECK(gpio_set_intr_type(GPIO_GPS_PPS, GPIO_INTR_POSEDGE));
  ESP_ERROR_CHECK(gpio_set_pull_mode(GPIO_GPS_PPS, GPIO_PULLDOWN_ONLY));

  ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT));
  ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_GPS_PPS, pps_isr_handler, NULL));
}

void time_sync_task(void *pvParameter) {
  const char *TAG = "time-sync";

  pps_init();

  uart_config_t uart_config;
  memset(&uart_config, 0, sizeof(uart_config_t));

  uart_config.baud_rate = 9600;
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity = UART_PARITY_DISABLE;
  uart_config.stop_bits = UART_STOP_BITS_1;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.source_clk = UART_SCLK_APB;

  uart_driver_install(UART_NUM, LEN_BUF_RX, LEN_BUF_TX, QUEUE_LEN, &uart_queue, 0);
  uart_param_config(UART_NUM, &uart_config);
  uart_set_pin(UART_NUM, GPIO_UART1_TXD, GPIO_UART1_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  uart_enable_pattern_det_baud_intr(UART_NUM, '\r', 1, 9, 0, 0);
  uart_pattern_queue_reset(UART_NUM, QUEUE_LEN);

  // disable all messages
  uart_write_bytes(UART_NUM, "$PUBX,40,GSA,0,0,0,0,0,0*4E\r\n", 29);
  uart_write_bytes(UART_NUM, "$PUBX,40,VTG,0,0,0,0,0,0*5E\r\n", 29);
  uart_write_bytes(UART_NUM, "$PUBX,40,RMC,0,0,0,0,0,0*47\r\n", 29);
  uart_write_bytes(UART_NUM, "$PUBX,40,GSV,0,0,0,0,0,0*59\r\n", 29);
  uart_write_bytes(UART_NUM, "$PUBX,40,GLL,0,0,0,0,0,0*5C\r\n", 29);

  // enable ZDA (date time)
  uart_write_bytes(UART_NUM, "$PUBX,40,ZDA,0,1,0,0,0,0*45\r\n", 29);

  // configure module to tx at 115200 baud
  uart_write_bytes(UART_NUM, "$PUBX,41,1,0007,0003,115200,0*18\r\n", 36);
  uart_wait_tx_done(UART_NUM, portMAX_DELAY);
  uart_set_baudrate(UART_NUM, 115200);

  uart_event_t evt;
  char buf_read[LEN_BUF_READ];
  int br = -1;

  while(1) {
    if(xQueueReceive(uart_queue, (void * )&evt, (portTickType)portMAX_DELAY)) {
      switch(evt.type) {
        case UART_PATTERN_DET:
        {
          uint32_t time;
          int day;
          int month;
          int year;

          memset(buf_read, 0, LEN_BUF_READ);

          uint8_t len = uart_pattern_pop_pos(UART_NUM) + 1;

          if (len > LEN_BUF_READ) {
            ESP_LOGW(TAG, "too long uart message of len=%d", len);
            uart_flush(UART_NUM);
            break;
          }

          // read up to the pattern
          br = uart_read_bytes(UART_NUM, (uint8_t *) buf_read, len, 0);

          char * buf_print = buf_read;
          // drop newline if exists
          if (*buf_print == '\n') buf_print ++;
          ESP_LOGD(TAG, "rx from gps: %s", buf_print);

          if (br > 0) {
            // leading \n because UART splits on \r
            if (sscanf(buf_read, "\n$GPZDA,%d.00,%d,%d,%d,00,00", &time, &day, &month, &year) == 4) {
              struct tm _tm;

              memset(&_tm, 0, sizeof(tm));

              _tm.tm_year = year - 1900;
              _tm.tm_mon = month - 1;
              _tm.tm_mday = day;
              _tm.tm_hour = time / 10000;
              _tm.tm_min = (time / 100) % 100;
              // + 1 because the next pps edge is the next second
              _tm.tm_sec = (time % 100) + 1;

              time_t t = mktime(&_tm);
              next_time.tv_sec = t;

              next_time_arrival = esp_timer_get_time();
            }
          } else {
            ESP_LOGW(TAG, "error reading bytes");
          }
          break;
        }
        default:
          break;
      }
    }
  }
}
