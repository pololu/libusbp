#include <tusb.h>
#include <device/usbd_pvt.h>

#define EP_ADDR_CDC_NOTIF   0x81
#define EP_ADDR_CDC_OUT     0x02
#define EP_ADDR_CDC_IN      0x82
#define EP_ADDR_ADC         0x83
#define EP_ADDR_CMD_OUT     0x04
#define EP_ADDR_CMD_IN      0x84

#define ADC_PACKET_SIZE 5
#define CMD_PACKET_SIZE 32

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
  .idProduct = 0x0007,
  .bcdDevice = 0x0100,
  .iManufacturer = 1,
  .iProduct = 2,
  .iSerialNumber = 3,
  .bNumConfigurations = 1
};

const uint8_t * tud_descriptor_device_cb()
{
  return (const uint8_t *)&desc_device;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 9 + 3 * 7 + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, 3, 0, CONFIG_TOTAL_LEN, 0xC0, 100),

  // Interface 0: native interface
  9, TUSB_DESC_INTERFACE, 0, 0, 3, TUSB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, 4,
  7, TUSB_DESC_ENDPOINT, EP_ADDR_ADC, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(ADC_PACKET_SIZE), 1,
  7, TUSB_DESC_ENDPOINT, EP_ADDR_CMD_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(CMD_PACKET_SIZE), 1,
  7, TUSB_DESC_ENDPOINT, EP_ADDR_CMD_IN, TUSB_XFER_BULK, U16_TO_U8S_LE(CMD_PACKET_SIZE), 1,

  // TODO: // Interface 1: native interface

  // CDC: first interface number, string index, notification EP & size, data endpoints & size
  // TODO: change notification size from 8 to 10 below
  TUD_CDC_DESCRIPTOR(1, 6, EP_ADDR_CDC_NOTIF, 8, EP_ADDR_CDC_OUT, EP_ADDR_CDC_IN, 64),
};

static_assert(CONFIG_TOTAL_LEN == sizeof(desc_configuration));

const uint8_t * tud_descriptor_configuration_cb(uint8_t __unused index)
{
  return desc_configuration;
}

typedef struct usb_desc_string_t
{
  uint8_t length;
  uint8_t type;
  uint16_t data[];
} usb_desc_string_t;
#define DESC_STRING(str) { sizeof(u##str), TUSB_DESC_STRING, u##str }

// TODO: static
const usb_desc_string_t
  language = { 4, TUSB_DESC_STRING, { 0x0409 } },
  manufacturer = DESC_STRING("Pololu Corporation"),
  product = DESC_STRING("USB Test Device A"),
  serial = DESC_STRING("123456"), // TODO: use unique ID
  interface0 = DESC_STRING("USB Test Device A Interface 0"),
  interface1 = DESC_STRING("USB Test Device A Interface 1"),
  port = DESC_STRING("USB Test Device A Port"),
  * const strings[] = {
    &language, [1] = &manufacturer, [2] = &product, [3] = &serial,
    [4] = &interface0, [5] = &interface1, [6] = &port };

const uint16_t * tud_descriptor_string_cb(uint8_t index, uint16_t __unused langid)
{
  if (index >= sizeof(strings) / sizeof(strings[0])) { return NULL; }
  return (const void *)strings[index];
}

//// vendor3: Driver for our vendor-defined interface //////////////////////////
// This driver only supports being attached to interface 0.

static void vendor3_init() {
}

static void vendor3_reset(uint8_t __unused rhport) {
}

static uint16_t vendor3_open(uint8_t __unused rhport,
  const tusb_desc_interface_t * itf_desc, uint16_t __unused max_len)
{
  TU_VERIFY(itf_desc->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC);
  TU_VERIFY(itf_desc->bInterfaceNumber == 0);
  TU_VERIFY(itf_desc->bNumEndpoints == 3, 0);

  return sizeof(tusb_desc_interface_t) + 3 * sizeof(tusb_desc_endpoint_t);
}

static bool vendor3_control_xfer_cb(uint8_t __unused rhport, uint8_t __unused stage,
  const tusb_control_request_t __unused * request)
{
  return false;
}

static bool vendor3_xfer_cb(uint8_t __unused rhport, uint8_t __unused ep_addr,
  xfer_result_t __unused result, uint32_t __unused xferred_bytes) {
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
