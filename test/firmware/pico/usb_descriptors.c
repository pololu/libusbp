/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "tusb.h"

#define EP_ADDR_CDC_NOTIF   0x81
#define EP_ADDR_CDC_OUT     0x02
#define EP_ADDR_CDC_IN      0x82

static const tusb_desc_device_t desc_device =
{
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,  // TODO: change to 0x0201, use MS OS 2.0 Descriptors

  .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

  // TODO: after we get the descriptors right, use a real VID/PID here
  .idVendor = 0xCAFE,
  .idProduct = 0x0006,
  .bcdDevice = 0x0100,
  .iManufacturer = 1,
  .iProduct = 2,
  .iSerialNumber = 3,
  .bNumConfigurations = 1
};

const uint8_t * tud_descriptor_device_cb()
{
  return (uint8_t const *)&desc_device;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN /*+ 9*/ + TUD_CDC_DESC_LEN)

static const uint8_t desc_fs_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, /*3*/ 2, 0, CONFIG_TOTAL_LEN, 0xC0, 100),

  // Interface 0: native interface
  //TPDP" 9, TUSB_DESC_INTERFACE, 0, 0, 0/*TODO: 3 eps*/, TUSB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, 4,
  // TODO:   7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0,

  // CDC: first interface number, string index, notification EP & size, data endpoints & size
  // TODO: change notification size from 8 to 10 below
  TUD_CDC_DESCRIPTOR(/*1*/ 0, 6, EP_ADDR_CDC_NOTIF, 8, EP_ADDR_CDC_OUT, EP_ADDR_CDC_IN, 64),
};

const uint8_t * tud_descriptor_configuration_cb(uint8_t index)
{
  (void)index;
  return desc_fs_configuration;
}

static const char * string_desc_arr[] =
{
  (const char[]) { 0x09, 0x04 },     // 0: Language is English (0x0409)
  "Pololu Corporation",              // 1: Manufacturer
  "USB Test Device A",               // 2: Product
  "123456",                          // 3: Serial number (TODO: use unique ID)
  "USB Test Device A Interface 0",   // 4
  "USB Test Device A Interface 1",   // 5
  "USB Test Device A Port",          // 6
};

static uint16_t string_desc[80];

// TODO: just store the USB string descriptors in flash instead of doing
// character conversions at runtime and imposing arbitrary size limits
const uint16_t * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void)langid;

  size_t char_count;

  if (index == 0)
  {
    memcpy(&string_desc[1], string_desc_arr[0], 2);
    char_count = 1;
  }
  else if (index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
  {
    const char * str = string_desc_arr[index];

    char_count = strlen(str);
    if (char_count > 79) { char_count = 79; }

    for (uint8_t i = 0; i < char_count; i++) { string_desc[i + 1] = str[i]; }
  }
  else
  {
    return NULL;
  }

  string_desc[0] = (uint16_t)(TUSB_DESC_STRING << 8 | (2 * char_count + 2));

  return string_desc;
}
