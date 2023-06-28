#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "bsp/board.h"
#include "tusb.h"

int main()
{
  board_init();
  tud_init(0);
  while (1)
  {
    tud_task();
  }
}
