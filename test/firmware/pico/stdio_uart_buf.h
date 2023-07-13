// Library that lets you connects the standard output (e.g. printf, putchar) to
// a ring buffer that is sent to the UART asynchronously.  Allows us to
// print detailed debugging information to the UART without slowing down
// real-time systems as much as the normal stdio_uart library would.
//
// This library is transmit-only: receiving data from the UART is not
// supported yet.

#pragma once

#include <hardware/uart.h>

#include <pico/stdio/driver.h>

extern stdio_driver_t stdio_uart_buf;

// Should only be called in the main loop, not in an interrupt.
void stdio_uart_buf_init(struct uart_inst * uart, uint baud_rate,
  int tx_pin, int rx_pin);

// Can be called in an interrupt.
size_t stdio_uart_buf_tx_available(void);

// Total number of bytes sent.  // TODO: just allow access to the variable?
size_t stdio_uart_buf_tx_send_count(void);

// Can be called in an interrupt.
size_t stdio_uart_buf_tx_write(const void * buf, size_t count);

// Can be called in an interrupt.
size_t stdio_uart_buf_tx_write_atom(const void * buf, size_t count);

// Should only be called in the main loop, not in an interrupt.
void stdio_uart_buf_task(void);
