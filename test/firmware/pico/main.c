// Firmware for USB Test Device A on a Raspberry Pi Pico (RP2040).
//
// Pinout:
// GP0  - TX: 3 Mbps UART signal for debugging
// GP25 - On-board LED

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <pico/bootrom.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>
#include <hardware/structs/ioqspi.h>
#include <hardware/structs/scb.h>
#include <hardware/structs/usb.h>
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

#define ADC_PACKET0_OFFSET 0xF80

#define ADC_PACKET0 ((volatile uint8_t *)(USBCTRL_DPRAM_BASE + ADC_PACKET0_OFFSET))
#define ADC_PACKET1 (ADC_PACKET0 + 64)

bool led_on;

// Variables for the command endpoint
size_t cmd_out_packet_length;
uint8_t CFG_TUSB_MEM_ALIGN cmd_out_buf[CMD_PACKET_SIZE];
uint8_t CFG_TUSB_MEM_ALIGN cmd_in_buf[CMD_PACKET_SIZE];

// The number of the next ADC data buffer we will prepare for the SIE.
bool adc_data_toggle;

bool adc_paused;
uint32_t adc_pause_start_time;
uint32_t adc_pause_duration;

// Generic buffer used for testing things
uint8_t data_buf[64 * 3 + 4];

static void handle_cmd_out_packet(void);


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
  cmd_out_packet_length = 0;
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

    // Set up the ADC endpoint.
    usb_dpram->ep_ctrl[(EP_ADDR_ADC & 0xF) - 1].in =
      EP_CTRL_ENABLE_BITS | EP_CTRL_DOUBLE_BUFFERED_BITS |
      (TUSB_XFER_INTERRUPT << 26) |
      ADC_PACKET0_OFFSET;
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
  if (result == XFER_RESULT_SUCCESS && ep_addr == EP_ADDR_CMD_OUT)
  {
    cmd_out_packet_length = transferred_bytes;
#if CFG_TUSB_DEBUG >= 2
    printf("Received command packet %u\n", cmd_out_packet_length);
#endif
    handle_cmd_out_packet();
    cmd_out_endpoint_transfer_start();
  }
  return true;
}

static const usbd_class_driver_t vendor3_driver =
{
#if CFG_TUSB_DEBUG > 1
  .name             = "Vendor3",
#endif
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

  if (request->bmRequestType == 0x40 && request->bRequest == 0x90)
  {
    // Command 0x90: Set LED
    led_on = request->wValue & 1;
    return tud_control_xfer(rhport, request, NULL, 0);
  }

  if (request->bmRequestType == 0xC0 && request->bRequest == 0x91)
  {
    // Command 0x91: Read Buffer
    // The length of the device's response will be equal to wIndex so this
    // request can be used to simulate what happens when a device returns less
    // data than expected.

    // TODO: can we remove some of these checks?
    if (request->wLength > sizeof(data_buf)) { return false; }
    if (request->wIndex > request->wLength) { return false; }

    sleep_ms(request->wValue);

    return tud_control_xfer(rhport, request, data_buf, request->wIndex);
  }

  if (request->bmRequestType == 0x40 && request->bRequest == 0x92)
  {
    // Command 0x92: Write buffer
    if (request->wLength > sizeof(data_buf)) { return false; } // TODO: remove?

    sleep_ms(request->wValue);

    return tud_control_xfer(rhport, request, data_buf, sizeof(data_buf));
  }

  if (request->bmRequestType == 0x40 && request->bRequest == 0xA0 &&
    request->wLength == 0)
  {
    // Command 0xA0: Pause or unpause the ADC data stream
    adc_paused = request->wValue ? 1 : 0;
    adc_pause_start_time = time_us_32();
    adc_pause_duration = request->wValue * 1000;
    return tud_control_xfer(rhport, request, NULL, 0);
  }

  return false;
}

static void handle_cmd_out_packet()
{
  if (cmd_out_packet_length == 0)
  {
    // Empty packet
    data_buf[0] = 0x66;
    return;
  }

  switch(cmd_out_buf[0])
  {
  case 0x92:  // Command 0x92: Set a byte in dataBuffer
    data_buf[0] = cmd_out_buf[1];
#if CFG_TUSB_DEBUG > 0
    printf("setting data_buf[0] to 0x%x\n", data_buf[0]);
#endif
    break;
  case 0xDE:  // Command 0xDE: Delay
    uint16_t delay = cmd_out_buf[1] | cmd_out_buf[2] << 8;
#if CFG_TUSB_DEBUG > 0
    printf("delaying for %u ms\n", delay);
#endif
    sleep_ms(delay);
    break;
  }
}

static void cdc_task()
{
  while (tud_cdc_available() && tud_cdc_write_available())
  {
    tud_cdc_write_char(tud_cdc_read_char());
  }
  tud_cdc_write_flush();
}

// Send ADC data to the USB host using our interrupt endpoint.
// Packet format:
// - byte 0: USB frame number
// - bytes 1 and 2: ADC reading (TODO)
// - bytes 3 and 4: ADC reading (TODO)
static void adc_task()
{
  if (adc_paused)
  {
    if ((uint32_t)(time_us_32() - adc_pause_start_time) < adc_pause_duration)
    {
      return;
    }
    adc_paused = 0;
  }

  volatile uint16_t * ctrl = (void *)USBCTRL_DPRAM_BASE +
    0x80 + 8 * (EP_ADDR_ADC & 0xF) + 2 * adc_data_toggle;

  uint16_t c = *ctrl;
  if (c & USB_BUF_CTRL_AVAIL) { return; }

  volatile uint8_t * packet = ADC_PACKET0 + 64 * adc_data_toggle;

  packet[0] = usb_hw->sof_rd;
  packet[1] = usb_hw->sof_rd >> 8;

  // TODO: fill the next 2 bytes of the packet with ADC data

  packet[4] = 0xAB;

  c = USB_BUF_CTRL_FULL | adc_data_toggle << 13 | ADC_PACKET_SIZE;
  *ctrl = c;
  asm volatile("nop\n" "nop\n" "nop\n");
  *ctrl = c | USB_BUF_CTRL_AVAIL;

  adc_data_toggle ^= 1;
}


//// Main code /////////////////////////////////////////////////////////////////

static void set_led(bool b)  // TODO: use this to indicate USB activity
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
    adc_task();
    set_led(led_on);

    // If the BOOTSEL button is pressed, launch the USB bootloader.
    // This makes it easier to reprogram the board.
    if (bootsel_button_pressed())
    {
      reset_usb_boot(0, 0);
    }

    // static uint32_t last_report_time = 0;
    // if ((uint32_t)(time_us_32() - last_report_time) > 1000000)
    // {
    //   printf("C %u\n", stdio_uart_buf_tx_send_count());
    //   printf("F %lu\n", usb_hw->sof_rd);
    //   last_report_time = time_us_32();
    // }
  }
}
