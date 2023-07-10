#include <hardware/gpio.h>
#include <pico/stdio/driver.h>
#include "stdio_uart_buf.h"

static uart_inst_t * uart_instance;

void stdio_uart_buf_init(struct uart_inst *uart, uint baud_rate,
  int tx_pin, int rx_pin)
{
  assert(rx_pin < 0);
  uart_instance = uart;
  if (tx_pin >= 0) gpio_set_function(tx_pin, GPIO_FUNC_UART);
  // uart_init(uart_instance, baud_rate);
  stdio_set_driver_enabled(&stdio_uart_buf, true);
  uart_init(uart_instance, baud_rate);
}

static void stdio_uart_buf_out_chars(const char *buf, int length)
{
  uart_putc(uart_instance, buf[0]); // tmphax
}

static int stdio_uart_buf_in_chars(char * buf, int length)
{
  // TODO: implement RX
}

stdio_driver_t stdio_uart_buf = {
  .out_chars = stdio_uart_buf_out_chars,
  .in_chars = stdio_uart_buf_in_chars,
// #if PICO_STDIO_UART_SUPPORT_CHARS_AVAILABLE_CALLBACK
//   .set_chars_available_callback = stdio_uart_buf_set_chars_available_callback,
// #endif
// #if PICO_STDIO_ENABLE_CRLF_SUPPORT
//   .crlf_enabled = PICO_STDIO_UART_DEFAULT_CRLF
// #endif
};
