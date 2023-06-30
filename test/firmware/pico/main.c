#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <pico/stdlib.h>
#include <pico/bootrom.h>
#include <hardware/structs/ioqspi.h>
#include <hardware/sync.h>

#include <tusb.h>

void led(bool b)  // TODO: use this to indicate USB activity
{
  gpio_init(25);
  gpio_put(25, b);
  gpio_set_dir(25, GPIO_OUT);
}

bool __no_inline_not_in_flash_func(bootsel_button_pressed)()
{
  uint32_t flags = save_and_disable_interrupts();

  hw_write_masked(&ioqspi_hw->io[1].ctrl,
                  GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                  IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

  // Delay for 1 to 2 us.
  uint32_t start = timer_hw->timerawl;
  while ((uint32_t)(timer_hw->timerawl - start) < 2);

  bool r = !(sio_hw->gpio_hi_in & (1 << 1));

  hw_clear_bits(&ioqspi_hw->io[1].ctrl, IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

  restore_interrupts(flags);

  return r;
}

int main()
{
  tud_init(0);
  while (1)
  {
    tud_task();

    // For ease of trying new firmware, if the BOOTSEL button is pressed,
    // launch the USB bootloader.
    if (bootsel_button_pressed())
    {
      reset_usb_boot(0, 0);
    }
  }
}
