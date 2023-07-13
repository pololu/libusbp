#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <pico/stdio/driver.h>
#include "stdio_uart_buf.h"

#ifndef STDIO_UART_BUF_SIZE_TX
#define STDIO_UART_BUF_SIZE_TX 1024
#endif

// TODO: can we remove volatile?
static volatile size_t tx_queue_count, tx_send_count;

static volatile uint8_t tx_buf[STDIO_UART_BUF_SIZE_TX];

static uart_inst_t * uart_instance;

static inline void __DMB(void) __attribute__((always_inline));
static inline void __DMB(void)
{
  asm volatile ("dmb 0xF":::"memory");
}

size_t stdio_uart_buf_tx_available()
{
  return sizeof(tx_buf) + tx_send_count - tx_queue_count;
}

size_t stdio_uart_buf_tx_write(const void * buf, size_t count)
{
  uint32_t flags = save_and_disable_interrupts();

  size_t available = stdio_uart_buf_tx_available();
  if (count > available) { count = available; }

  // Note: Calling memcpy here would be more efficient, but we have to call it
  // twice if we wrap around.
  for (size_t i = 0; i < count; i++)
  {
    tx_buf[(tx_queue_count + i) % sizeof(tx_buf)] = ((uint8_t *)buf)[i];
  }
  tx_queue_count += count;

  restore_interrupts(flags);
  return count;
}

// This function silently discards characters if there is no room in the
// buffer.
static void stdio_uart_buf_out_chars(const char * buf, int length)
{
  stdio_uart_buf_tx_write(buf, length);
}

static int stdio_uart_buf_in_chars(char * buf, int length)
{
  (void)buf; (void)length;
  return 0; // TODO: implement RX
}

void stdio_uart_buf_init(struct uart_inst *uart, uint baud_rate,
  int tx_pin, int rx_pin)
{
  assert(rx_pin < 0);
  uart_instance = uart;

  if (tx_pin >= 0) gpio_set_function(tx_pin, GPIO_FUNC_UART);

  uart_init(uart_instance, baud_rate);
  stdio_set_driver_enabled(&stdio_uart_buf, true);
}

void stdio_uart_buf_task(void)
{
  while (tx_send_count != tx_queue_count && uart_is_writable(uart_instance))
  {
    uart_get_hw(uart_instance)->dr = tx_buf[tx_send_count % sizeof(tx_buf)];
    tx_send_count++;
  }
  __DMB();
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
