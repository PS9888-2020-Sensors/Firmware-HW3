#include "driver/uart.h"
#include "string.h"

#include "dump_data.h"
#include "board.h"

static QueueHandle_t uart_queue;
static const int UART_NUM = UART_NUM_2;
static const int LEN_BUF_TX = 16384;
static const int LEN_BUF_RX = 256;
static const int QUEUE_LEN = 8;

void dump_data_init() {
  uart_config_t uart_config;
  memset(&uart_config, 0, sizeof(uart_config_t));

  uart_config.baud_rate = 921600;
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity = UART_PARITY_DISABLE;
  uart_config.stop_bits = UART_STOP_BITS_1;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.source_clk = UART_SCLK_APB;

  uart_driver_install(UART_NUM, LEN_BUF_RX, LEN_BUF_TX, QUEUE_LEN, &uart_queue, 0);
  uart_param_config(UART_NUM, &uart_config);
  uart_set_pin(UART_NUM, GPIO_UART1_TXD, GPIO_UART1_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void dump_data(uint8_t *mac, uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw) {
  uart_write_bytes(UART_NUM, "DATAPAKT", 8);
  uart_write_bytes(UART_NUM, (const char *) mac, 6);
  uart_write_bytes(UART_NUM, (const char *) &file_index, 2);
  uart_write_bytes(UART_NUM, (const char *) &file_offset, 4);
  uart_write_bytes(UART_NUM, (const char *) data, btw);
  uart_write_bytes(UART_NUM, "ENDPAKT", 7);
}
