// Library that lets you connects the standard output (e.g. printf, putchar) to
// a ring buffer that is sent to the UART asynchronously.  Allows us to
// print detailed debugging information to the UART without slowing down
// real-time systems as much as the normal stdio_uart library would.
//
// This library is transmit-only: receiving data from the UART is not
// supported yet.

#pragma once

#include "hardware/uart.h"

extern stdio_driver_t stdio_uart_buf;

void stdio_uart_buf_init(struct uart_inst * uart, uint baud_rate,
  int tx_pin, int rx_pin);

size_t stdio_uart_buf_tx_available();

size_t stdio_uart_buf_tx_write(const void * buf, size_t count);
