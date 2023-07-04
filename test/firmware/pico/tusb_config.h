#pragma once

#ifdef NDEBUG
#define CFG_TUSB_DEBUG 0
#else
// TODO: change this to 3 after we've implemented a non-blocking stdio driver.
// Even if the driver doesn't DO anything, we want to make sure we can set this
// to 3 and have the device successfully enumerate.
#define CFG_TUSB_DEBUG 0
#endif

#define CFG_TUD_ENABLED 1

#define CFG_TUD_CDC 1

#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 64
