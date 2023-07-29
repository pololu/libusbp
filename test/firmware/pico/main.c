#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <pico/bootrom.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>
#include <hardware/structs/ioqspi.h>
#include <hardware/sync.h>

#include <tusb.h>
#include <device/usbd_pvt.h>

#include "stdio_uart_buf.h"

#define EP_ADDR_CDC_NOTIF   0x81
#define EP_ADDR_CDC_OUT     0x02  // was 4
#define EP_ADDR_CDC_IN      0x82
#define EP_ADDR_ADC         0x83  // was 2
#define EP_ADDR_CMD_OUT     0x04  // was 3
#define EP_ADDR_CMD_IN      0x84

#define ADC_PACKET_SIZE 5
#define CMD_PACKET_SIZE 32

// Buffers used to send and receive data on the command endpoint
bool cmd_out_packet_received;
size_t cmd_out_packet_length;
uint8_t CFG_TUSB_MEM_ALIGN cmd_out_buf[CMD_PACKET_SIZE];
uint8_t CFG_TUSB_MEM_ALIGN cmd_in_buf[CMD_PACKET_SIZE];

// Generic buffer used for testing things
uint8_t data_buf[64 * 3 + 4];


//// USB descriptors ///////////////////////////////////////////////////////////

typedef struct usb_desc_string_t
{
  uint8_t length;
  uint8_t type;
  uint16_t data[];
} usb_desc_string_t;
#define DESC_STRING(str) { sizeof(u##str), TUSB_DESC_STRING, u##str }

static_assert(PICO_UNIQUE_BOARD_ID_SIZE_BYTES == 8);
static usb_desc_string_t usb_string_serial = {0, 0, u"1122334455667788"};

static char nibble_to_hex(uint8_t n)
{
  return n < 10 ? '0' + n : ('A' - 10) + n;
}

static void usb_string_serial_init()
{
  pico_unique_board_id_t uid;
  pico_get_unique_board_id(&uid);
  usb_string_serial.length = 2 + 4 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES;
  usb_string_serial.type = TUSB_DESC_STRING;
  for (size_t i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
  {
    usb_string_serial.data[2 * i + 0] = nibble_to_hex(uid.id[i] >> 4);
    usb_string_serial.data[2 * i + 1] = nibble_to_hex(uid.id[i] & 0xF);
  }
}

static const usb_desc_string_t
  language = { 4, TUSB_DESC_STRING, { 0x0409 } },
  manufacturer = DESC_STRING("Pololu Corporation"),
  product = DESC_STRING("USB Test Device A"),
  interface0 = DESC_STRING("USB Test Device A Interface 0"),
  interface1 = DESC_STRING("USB Test Device A Interface 1"),
  port = DESC_STRING("USB Test Device A Port"),
  * const strings[] = {
    &language, [1] = &manufacturer, [2] = &product, [3] = &usb_string_serial,
    [4] = &interface0, [5] = &interface1, [6] = &port };

const uint16_t * tud_descriptor_string_cb(uint8_t index, uint16_t __unused langid)
{
  if (index >= sizeof(strings) / sizeof(strings[0])) { return NULL; }
  if (usb_string_serial.length == 0) { usb_string_serial_init(); }
  return (const void *)strings[index];
}

static const tusb_desc_device_t desc_device =
{
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0201,

  .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor = 0x1FFB,   // Pololu Corporation
  .idProduct = 0xDA01,  // USB Test Device A
  .bcdDevice = 0x0008,
  .iManufacturer = 1,
  .iProduct = 2,
  .iSerialNumber = 3,
  .bNumConfigurations = 1
};

const uint8_t * tud_descriptor_device_cb()
{
  return (const uint8_t *)&desc_device;
}

#define CONFIG_LENGTH (TUD_CONFIG_DESC_LEN + 9 + 3 * 7 + 9 + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_LENGTH, 0xC0, 100),

  // Interface 0: native interface
  9, TUSB_DESC_INTERFACE, 0, 0, 3, TUSB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, 4,
  7, TUSB_DESC_ENDPOINT, EP_ADDR_ADC, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(ADC_PACKET_SIZE), 1,
  7, TUSB_DESC_ENDPOINT, EP_ADDR_CMD_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(CMD_PACKET_SIZE), 1,
  7, TUSB_DESC_ENDPOINT, EP_ADDR_CMD_IN, TUSB_XFER_BULK, U16_TO_U8S_LE(CMD_PACKET_SIZE), 1,

  // Interface 1: another native interface
  9, TUSB_DESC_INTERFACE, 1, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, 5,

  // CDC: first interface number, string index, notification EP & size, data endpoints & size
  TUD_CDC_DESCRIPTOR(2, 6, EP_ADDR_CDC_NOTIF, 10, EP_ADDR_CDC_OUT, EP_ADDR_CDC_IN, 64),
};

static_assert(CONFIG_LENGTH == sizeof(desc_configuration));

const uint8_t * tud_descriptor_configuration_cb(uint8_t __unused index)
{
  return desc_configuration;
}


//// Microsoft OS 2.0 descriptors and BOS //////////////////////////////////////

// Wireless USB Specification 1.1, Table 7-1
#define USB_DESCRIPTOR_TYPE_BOS 15
#define USB_DESCRIPTOR_TYPE_DEVICE_CAPABILITY 16

// Microsoft OS 2.0 Descriptors, Table 1
#define USB_DEVICE_CAPABILITY_TYPE_PLATFORM 5

// Microsoft OS 2.0 Descriptors, Table 8
#define MS_OS_20_DESCRIPTOR_INDEX 7

// Microsoft OS 2.0 Descriptors, Table 9
#define MS_OS_20_FEATURE_COMPATIBLE_ID 0x03

#define REQUEST_GET_MS_DESCRIPTOR 0x20

#define MS_OS_20_LENGTH 0xB2

const uint8_t desc_ms_os_20[] = {
  // Microsoft OS 2.0 Descriptor Set header (Table 10)
  0x0A, 0x00,  // wLength
  MS_OS_20_SET_HEADER_DESCRIPTOR, 0x00,
  0x00, 0x00, 0x03, 0x06,  // dwWindowsVersion: Windows 8.1 (NTDDI_WINBLUE)
  MS_OS_20_LENGTH, 0x00,  // wTotalLength

  // Microsoft OS 2.0 configuration subset (Table 11)
  0x08, 0x00,   // wLength of this header
  MS_OS_20_SUBSET_HEADER_CONFIGURATION, 0x00,  // wDescriptorType
  0,            // configuration index
  0x00,         // bReserved
  0xA8, 0x00,   // wTotalLength of this subset

  // Microsoft OS 2.0 function subset header (Table 12)
  0x08, 0x00,  // wLength
  MS_OS_20_SUBSET_HEADER_FUNCTION, 0x00,  // wDescriptorType
  0,           // bFirstInterface
  0x00,        // bReserved,
  0xA0, 0x00,  // wSubsetLength

  // Microsoft OS 2.0 compatible ID descriptor (Table 13)
  0x14, 0x00,                                      // wLength
  MS_OS_20_FEATURE_COMPATIBLE_ID, 0x00,            // wDescriptorType
  'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,        // compatibleID
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // subCompatibleID

  // Microsoft OS 2.0 registry property descriptor (Table 14)
  0x84, 0x00,   // wLength
  MS_OS_20_FEATURE_REG_PROPERTY, 0x00,
  0x07, 0x00,   // wPropertyDataType: REG_MULTI_SZ
  0x2a, 0x00,   // wPropertyNameLength
  'D',0,'e',0,'v',0,'i',0,'c',0,'e',0,'I',0,'n',0,'t',0,'e',0,'r',0,
  'f',0,'a',0,'c',0,'e',0,'G',0,'U',0,'I',0,'D',0,'s',0,0,0,
  0x50, 0x00,   // wPropertyDataLength
  // GUID that represents libusbp test devices (generate your own GUID
  // if you are copying this example for another device).
  '{',0,'9',0,'9',0,'c',0,'4',0,'b',0,'b',0,'b',0,'0',0,'-',0,
  'e',0,'9',0,'2',0,'5',0,'-',0,'4',0,'3',0,'9',0,'7',0,'-',0,
  'a',0,'f',0,'e',0,'e',0,'-',0,'9',0,'8',0,'1',0,'c',0,'d',0,
  '0',0,'7',0,'0',0,'2',0,'1',0,'6',0,'3',0,'}',0,0,0,0,0,
};

static_assert(MS_OS_20_LENGTH == sizeof(desc_ms_os_20));

#define BOS_LENGTH 0x21

const uint8_t desc_bos[] =
{
  0x05,       // bLength of this descriptor
  USB_DESCRIPTOR_TYPE_BOS,
  BOS_LENGTH, 0x00, // wLength
  0x01,       // bNumDeviceCaps

  0x1C,       // bLength of this first device capability descriptor
  USB_DESCRIPTOR_TYPE_DEVICE_CAPABILITY,
  USB_DEVICE_CAPABILITY_TYPE_PLATFORM,
  0x00,       // bReserved
  // Microsoft OS 2.0 descriptor platform capability UUID
  // from Microsoft OS 2.0 Descriptors,  Table 3.
  0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
  0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,

  0x00, 0x00, 0x03, 0x06,   // dwWindowsVersion: Windows 8.1 (NTDDI_WINBLUE)
  MS_OS_20_LENGTH, 0x00,    // wMSOSDescriptorSetTotalLength
  REQUEST_GET_MS_DESCRIPTOR,
  0,                        // bAltEnumCode
};

static_assert(BOS_LENGTH == sizeof(desc_bos));

const uint8_t * tud_descriptor_bos_cb()
{
  return desc_bos;
}


//// vendor3: TinyUSB driver for our vendor-defined interfaces /////////////////

static void cmd_out_endpoint_transfer_start()
{
  memset(cmd_out_buf, 0, sizeof(cmd_out_buf));
  usbd_edpt_xfer(0, EP_ADDR_CMD_OUT, cmd_out_buf, sizeof(cmd_out_buf));
}

static void vendor3_init() {
}

static void vendor3_reset(uint8_t __unused rhport) {
}

static uint16_t vendor3_open(uint8_t rhport,
  const tusb_desc_interface_t * interface_descriptor, uint16_t __unused max_len)
{
  TU_VERIFY(interface_descriptor->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC);

  const void * descriptor = interface_descriptor;
  while (1)
  {
    descriptor = tu_desc_next(descriptor);
    if (tu_desc_type(descriptor) == TUSB_DESC_INTERFACE) { break; }
    if (tu_desc_type(descriptor) == TUSB_DESC_ENDPOINT)
    {
      TU_VERIFY(usbd_edpt_open(rhport, descriptor));
    }
  }

  if (interface_descriptor->bNumEndpoints != 0)
  {
    cmd_out_endpoint_transfer_start();
  }

  return descriptor - (const void *)interface_descriptor;
}

static bool vendor3_control_xfer_cb(uint8_t __unused rhport, uint8_t __unused stage,
  const tusb_control_request_t __unused * request)
{
  return false;
}

static bool vendor3_xfer_cb(uint8_t __unused rhport, uint8_t ep_addr,
  xfer_result_t result, uint32_t transferred_bytes) {
  if (result == XFER_RESULT_SUCCESS && ep_addr == EP_ADDR_CDC_OUT)
  {
    cmd_out_packet_received = true;
    cmd_out_packet_length = transferred_bytes;
#if CFG_TUSB_DEBUG >= 2
    printf("Received command packet %u\n", cmd_out_packet_length);
#endif
  }
  return true;
}

static const usbd_class_driver_t vendor3_driver =
{
  .name             = "Vendor3",
  .init             = vendor3_init,
  .reset            = vendor3_reset,
  .open             = vendor3_open,
  .control_xfer_cb  = vendor3_control_xfer_cb,
  .xfer_cb          = vendor3_xfer_cb,
};

const usbd_class_driver_t * usbd_app_driver_get_cb(uint8_t * driver_count)
{
  *driver_count = 1;
  return &vendor3_driver;
}


//// USB data processing ///////////////////////////////////////////////////////

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
  const tusb_control_request_t * request)
{
  if (stage != CONTROL_STAGE_SETUP) { return true; }

  if (request->bmRequestType == 0xC0
    && request->bRequest == REQUEST_GET_MS_DESCRIPTOR
    && request->wIndex == MS_OS_20_DESCRIPTOR_INDEX)
  {
    return tud_control_xfer(rhport, request, (void *)desc_ms_os_20, sizeof(desc_ms_os_20));
  }

  return false;
}

static void cmd_task()
{
  if (!cmd_out_packet_received) { return; }

  if (cmd_out_packet_length == 0)
  {
    // Empty packet
    data_buf[0] = 0x66;
  }

  switch(cmd_out_buf[0])
  {
  case 0x92:  // Set a byte in dataBuffer
    data_buf[0] = cmd_out_buf[1];
    break;
  case 0xDE:  // Delay
    uint16_t delay = cmd_out_buf[1] | cmd_out_buf[2] << 8;
    sleep_ms(delay);
    break;
  }

  cmd_out_endpoint_transfer_start();
}

static void cdc_task()
{
  while (tud_cdc_available() && tud_cdc_write_available())
  {
    tud_cdc_write_char(tud_cdc_read_char());
  }
  tud_cdc_write_flush();
}


//// Main code /////////////////////////////////////////////////////////////////

static void led(bool b)  // TODO: use this to indicate USB activity
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
  // GP0 = TX, 3 Mbps (max speed of the CP2102N)
  stdio_uart_buf_init(uart0, 3000000, 0, -1);
  tud_init(0);
  while (1)
  {
    tud_task();
    stdio_uart_buf_task();
    cdc_task();
    cmd_task();

    static uint32_t last_report_time = 0;
    if ((uint32_t)(time_us_32() - last_report_time) > 8000000)
    {
      led(1);
      printf("%u\n", stdio_uart_buf_tx_send_count());
      led(0);
      last_report_time = time_us_32();
    }

    // If the BOOTSEL button is pressed, launch the USB bootloader.
    // This makes it easier to reprogram the board.
    if (bootsel_button_pressed())
    {
      reset_usb_boot(0, 0);
    }
  }
}
