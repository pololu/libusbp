#pragma once

#ifdef NDEBUG
#define CFG_TUSB_DEBUG 0
#else
#define CFG_TUSB_DEBUG 2
#endif

#define CFG_TUD_ENABLED 1

#define CFG_TUD_CDC 1

#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 64
