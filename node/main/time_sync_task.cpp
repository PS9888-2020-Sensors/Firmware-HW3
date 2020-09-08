#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include <sys/time.h>

#include "board.h"

static QueueHandle_t uart_queue;
static const int UART_NUM = UART_NUM_2;
static const int LEN_BUF_TX = 256;
static const int LEN_BUF_RX = 256;
// GPZDA looks to be max 36 chars + 2 for CRLF + 1 for NULL
static const int LEN_BUF_READ = 40;
static const int QUEUE_LEN = 8;

void time_sync_task(void *pvParameter) {
  const char *TAG = "time-sync";

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

  // configure module to tx at 115200 baud
  uart_write_bytes(UART_NUM, "$PUBX,41,1,0007,0003,115200,0*18\r\n", 36);
  uart_wait_tx_done(UART_NUM, portMAX_DELAY);
  uart_set_baudrate(UART_NUM, 115200);

  // disable all messages
  uart_write_bytes(UART_NUM, "$PUBX,40,GSA,0,0,0,0,0,0*4E\r\n", 29);
  uart_write_bytes(UART_NUM, "$PUBX,40,VTG,0,0,0,0,0,0*5E\r\n", 29);
  uart_write_bytes(UART_NUM, "$PUBX,40,RMC,0,0,0,0,0,0*47\r\n", 29);
  uart_write_bytes(UART_NUM, "$PUBX,40,GSV,0,0,0,0,0,0*59\r\n", 29);
  uart_write_bytes(UART_NUM, "$PUBX,40,GLL,0,0,0,0,0,0*5C\r\n", 29);

  // enable ZDA (date time)
  uart_write_bytes(UART_NUM, "$PUBX,40,ZDA,0,1,0,0,0,0*45\r\n", 29);

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

          ESP_LOGI(TAG, "pat detected %s", buf_read);

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
              _tm.tm_sec = time % 100;

              time_t t = mktime(&_tm);

              ESP_LOGI(TAG, "set time to %s", asctime(&_tm));

              struct timeval now;
              now.tv_sec = t;

              settimeofday(&now, NULL);
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
