#include <hardware/gpio.h>
#include <pico/stdio/driver.h>
#include "stdio_uart_buf.h"

static inline void __DMB(void) __attribute__((always_inline));
static inline void __DMB(void)
{
  asm volatile ("dmb 0xF":::"memory");
}

static uart_inst_t * uart_instance;

static void uart_isr(void)
{
  uart_get_hw(uart_instance)->dr = 'q';
  uart_set_irq_enables(uart_instance, false, false);
}

size_t stdio_uart_buf_tx_available(); // TODO

size_t stdio_uart_buf_tx_write(const void * buf, size_t count)
{
  // TODO: stop calling uart_set_irq_enables so we can set the thresholds
  // in the IFLS register to something smart and not waste time setting them
  // many times.
  uart_set_irq_enables(uart_instance, false, true);
  return 0;  // tmphax
}

static void stdio_uart_buf_out_chars(const char * buf, int length)
{
  if (length < 0) { return; }
  uart_putc(uart_instance, 'a'); // tmphax, but some reason if I remove this the qs go away
  stdio_uart_buf_tx_write(buf, length);
}

static int stdio_uart_buf_in_chars(char * buf, int length)
{
  return 0; // TODO: implement RX
}

void stdio_uart_buf_init(struct uart_inst *uart, uint baud_rate,
  int tx_pin, int rx_pin)
{
  assert(rx_pin < 0);
  uart_instance = uart;
  __DMB();  // memory barrier to ensure ISR sees correct uart_instance

  if (tx_pin >= 0) gpio_set_function(tx_pin, GPIO_FUNC_UART);
  // uart_init(uart_instance, baud_rate);

  uint irq = UART0_IRQ + uart_get_index(uart_instance);
  irq_set_exclusive_handler(irq, uart_isr);
  irq_set_enabled(irq, true);
  uart_set_irq_enables(uart_instance, false, true);

  stdio_set_driver_enabled(&stdio_uart_buf, true);
  uart_init(uart_instance, baud_rate);
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
