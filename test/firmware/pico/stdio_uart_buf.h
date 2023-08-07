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

/// Initializes the library and the UART.
///
/// Should only be called in the main loop, not in an interrupt.
void stdio_uart_buf_init(struct uart_inst * uart, uint baud_rate,
  int tx_pin, int rx_pin);

/// Returns the number of bytes free in the TX buffer.
///
/// This is the number of bytes that can be successfully written now.
///
/// This function can be called in an interrupt.
size_t stdio_uart_buf_tx_available(void);

/// Total number of bytes sent that have actually been sent on the UART.
/// (Wraps around eventually.)
size_t stdio_uart_buf_tx_send_count(void);

/// Attempts to write data to the TX buffer to be transmitted.
///
/// If there is not enough space in the TX buffer, then this function only
/// writes a portion of the data.  This function does *NOT* record the amount
/// of data lost.  See void stdio_uart_buf_out_chars() if you want that.
///
/// Returns the number of bytes successfully written.
///
/// This function can be called in an interrupt.
size_t stdio_uart_buf_tx_write(const void * buf, size_t count);

/// Attempts to write data to the TX buffer to be transmitted.
///
/// If there is not enough space in the TX buffer to write the entire amount
/// specified, this function returns without doing anything.
///
/// Returns the number of bytes successfully written (0 or count).
size_t stdio_uart_buf_tx_write_atom(const void * buf, size_t count);

/// Attempts to write data to the TX buffer to be transmitted.
///
/// Standard I/O functions like printf and putchar call this function, assuming
/// you have initialized the library.
///
/// If there is not enough space in the TX buffer to write the entire amount
/// specified, this function will write as much as it can and then records the
/// number of bytes it had to cut, so that number can be transmitted later.
///
/// Unlike the stdio_uart_buf_tx_write* functions, this function never writes
/// data to the TX buffer if bytes were recently cut and the message to report
/// them has not yet been queued in the TX buffer.  This ensures that the
/// payload data and the cut notifications appear in the right order, if you
/// exclusively use this function for any writes that might fail.
///
/// This function can be called in an interrupt.
void stdio_uart_buf_out_chars(const char * buf, int length);

/// This function should be called regularly in the main loop of the program.
void stdio_uart_buf_task(void);

/// Calls stdio_uart_buf_task() until all the data we are trying to send has
/// actually been written to the UART.
///
/// Note that the UART itself has a 32-byte buffer and this function does not
/// wait for its transmissions to finish.
void stdio_uart_buf_flush(void);
