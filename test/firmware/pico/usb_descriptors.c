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
  .bcdUSB = 0x0201,

  .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

  // TODO: after we get the descriptors right, use a real VID/PID here
  .idVendor = 0xCAFE,
  .idProduct = 0x0009,
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

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 9 + 3 * 7 + 9 + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_TOTAL_LEN, 0xC0, 100),

  // Interface 0: native interface
  9, TUSB_DESC_INTERFACE, 0, 0, 3, TUSB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, 4,
  7, TUSB_DESC_ENDPOINT, EP_ADDR_ADC, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(ADC_PACKET_SIZE), 1,
  7, TUSB_DESC_ENDPOINT, EP_ADDR_CMD_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(CMD_PACKET_SIZE), 1,
  7, TUSB_DESC_ENDPOINT, EP_ADDR_CMD_IN, TUSB_XFER_BULK, U16_TO_U8S_LE(CMD_PACKET_SIZE), 1,

  // Interface 1: another native interface
  9, TUSB_DESC_INTERFACE, 1, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, 5,

  // CDC: first interface number, string index, notification EP & size, data endpoints & size
  // TODO: change notification size from 8 to 10 below
  TUD_CDC_DESCRIPTOR(2, 6, EP_ADDR_CDC_NOTIF, 8, EP_ADDR_CDC_OUT, EP_ADDR_CDC_IN, 64),
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

//// Microsoft OS 2.0 descriptors and BOS //////////////////////////////////////

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
  // if you are copying this example for your own device)
  '{',0,'9',0,'9',0,'c',0,'4',0,'b',0,'b',0,'b',0,'0',0,'-',0,
  'e',0,'9',0,'2',0,'5',0,'-',0,'4',0,'3',0,'9',0,'7',0,'-',0,
  'a',0,'f',0,'e',0,'e',0,'-',0,'9',0,'8',0,'1',0,'c',0,'d',0,
  '0',0,'7',0,'0',0,'2',0,'1',0,'6',0,'3',0,'}',0,0,0,0,0,
};

static_assert(MS_OS_20_LENGTH == sizeof(desc_ms_os_20));

#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

const uint8_t desc_bos[] =
{
  // total length, number of device caps
  TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),

  TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_LENGTH, REQUEST_GET_MS_DESCRIPTOR)
};

static_assert(BOS_TOTAL_LEN == sizeof(desc_bos));

const uint8_t * tud_descriptor_bos_cb()
{
  return desc_bos;
}

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


//// vendor3: Driver for our vendor-defined interfaces /////////////////////////

static void vendor3_init() {
}

static void vendor3_reset(uint8_t __unused rhport) {
}

static uint16_t vendor3_open(uint8_t __unused rhport,
  const tusb_desc_interface_t * itf_desc, uint16_t __unused max_len)
{
  TU_VERIFY(itf_desc->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC);

  // Assumption: the only descriptors in this interface are endpoint descriptors
  return sizeof(tusb_desc_interface_t) +
    itf_desc->bNumEndpoints * sizeof(tusb_desc_endpoint_t);
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
