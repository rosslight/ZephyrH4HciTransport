/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <hal/nrf_ficr.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/bos.h>
#include <zephyr/usb/msos_desc.h>

#define DFU_DETACH 0x00
#define DFU_FIRMWARE_INFO 0x07
#define DFU_VERSION_STRING 0x08
#define MS_OS_VENDOR_REQUEST 0x02
#define MS_OS_WINDOWS_VERSION 0x06030000

static const struct gpio_dt_spec dfu_reset =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), dfu_reset_gpios);
static bool reset_ready;

static struct {
	uint32_t address;
	uint32_t firmware_size;
	uint16_t version_major;
	uint16_t version_minor;
	uint32_t firmware_id;
	uint32_t flash_size;
	uint32_t flash_page_size;
} __packed firmware_info;

static uint8_t version_string[] = "ZephyrH4HciTransport " NORDIC_DFU_VERSION;
extern char _flash_used[];

struct dfu_functional_descriptor {
	uint8_t length;
	uint8_t type;
	uint8_t attributes;
	uint16_t detach_timeout;
	uint16_t transfer_size;
	uint16_t dfu_version;
} __packed;

#if CONFIG_USB_DEVICE_VID == 0x1915 && CONFIG_USB_DEVICE_PID == 0xC00A
/* The Nordic Windows driver binds to interface 0. Sort before the CDC ACM descriptor. */
#define DFU_INTERFACE_DESCRIPTOR static __in_section(usb, descriptor_primary, 1) __used __aligned(1)
#else
#define DFU_INTERFACE_DESCRIPTOR USBD_CLASS_DESCR_DEFINE(primary, 99)
#endif

/* Nordic's runtime interface uses endpoint zero for all requests. */
DFU_INTERFACE_DESCRIPTOR struct {
	struct usb_if_descriptor interface;
	struct dfu_functional_descriptor functional;
} __packed dfu_descriptor = {
	.interface = {
		.bLength = sizeof(struct usb_if_descriptor),
		.bDescriptorType = USB_DESC_INTERFACE,
		.bInterfaceClass = USB_BCC_VENDOR,
		.bInterfaceSubClass = 0x01,
		.bInterfaceProtocol = 0x01,
	},
	.functional = {
		.length = sizeof(struct dfu_functional_descriptor),
		.type = 0x21,
		.attributes = BIT(0) | BIT(3),
		.dfu_version = sys_cpu_to_le16(0x0110),
	},
};

#define WINUSB_FUNCTION_LENGTH (sizeof(struct msosv2_function_subset_header) + \
				sizeof(struct msosv2_compatible_id) + \
				sizeof(struct msosv2_guids_property))
#define WINUSB_CONFIGURATION_LENGTH (sizeof(struct msosv2_configuration_subset_header) + \
				     WINUSB_FUNCTION_LENGTH)

static struct {
	struct msosv2_descriptor_set_header header;
	struct msosv2_configuration_subset_header configuration;
	struct msosv2_function_subset_header function;
	struct msosv2_compatible_id compatible_id;
	struct msosv2_guids_property guid;
} __packed winusb_descriptor = {
	.header = {
		.wLength = sys_cpu_to_le16(sizeof(struct msosv2_descriptor_set_header)),
		.wDescriptorType = sys_cpu_to_le16(MS_OS_20_SET_HEADER_DESCRIPTOR),
		.dwWindowsVersion = sys_cpu_to_le32(MS_OS_WINDOWS_VERSION),
		.wTotalLength = sys_cpu_to_le16(sizeof(winusb_descriptor)),
	},
	.configuration = {
		.wLength = sys_cpu_to_le16(sizeof(struct msosv2_configuration_subset_header)),
		.wDescriptorType = sys_cpu_to_le16(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
		.wTotalLength = sys_cpu_to_le16(WINUSB_CONFIGURATION_LENGTH),
	},
	.function = {
		.wLength = sys_cpu_to_le16(sizeof(struct msosv2_function_subset_header)),
		.wDescriptorType = sys_cpu_to_le16(MS_OS_20_SUBSET_HEADER_FUNCTION),
		.wSubsetLength = sys_cpu_to_le16(WINUSB_FUNCTION_LENGTH),
	},
	.compatible_id = {
		.wLength = sys_cpu_to_le16(sizeof(struct msosv2_compatible_id)),
		.wDescriptorType = sys_cpu_to_le16(MS_OS_20_FEATURE_COMPATIBLE_ID),
		.CompatibleID = "WINUSB",
	},
	.guid = {
		.wLength = sys_cpu_to_le16(sizeof(struct msosv2_guids_property)),
		.wDescriptorType = sys_cpu_to_le16(MS_OS_20_FEATURE_REG_PROPERTY),
		.wPropertyDataType = sys_cpu_to_le16(MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ),
		.wPropertyNameLength = sys_cpu_to_le16(sizeof(winusb_descriptor.guid.PropertyName)),
		.PropertyName = { DEVICE_INTERFACE_GUIDS_PROPERTY_NAME },
		.wPropertyDataLength = sys_cpu_to_le16(sizeof(winusb_descriptor.guid.bPropertyData)),
	},
};

USB_DEVICE_BOS_DESC_DEFINE_CAP struct {
	struct usb_bos_platform_descriptor platform;
	struct usb_bos_capability_msos capability;
} __packed winusb_capability = {
	.platform = {
		.bLength = sizeof(winusb_capability),
		.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
		.bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
		.PlatformCapabilityUUID = {
			/* Microsoft OS 2.0 platform capability UUID. */
			0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
			0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
		},
	},
	.capability = {
		.dwWindowsVersion = sys_cpu_to_le32(MS_OS_WINDOWS_VERSION),
		.wMSOSDescriptorSetTotalLength = sys_cpu_to_le16(sizeof(winusb_descriptor)),
		.bMS_VendorCode = MS_OS_VENDOR_REQUEST,
	},
};

uint8_t *usb_update_sn_string_descriptor(void)
{
	static uint8_t serial[sizeof(CONFIG_USB_DEVICE_SN)];
	uint16_t high = (uint16_t)NRF_FICR->DEVICEADDR[1] | 0xC000;

	/* Match the factory nRF5 bootloader's random-static address format. */
	BUILD_ASSERT(sizeof(serial) == 13);
	snprintf((char *)serial, sizeof(serial), "%04X%08X", (unsigned int)high,
		 (unsigned int)NRF_FICR->DEVICEADDR[0]);
	return serial;
}

static void enter_bootloader(struct k_work *work)
{
	ARG_UNUSED(work);
	gpio_pin_set_dt(&dfu_reset, 1);
}

static K_WORK_DELAYABLE_DEFINE(reset_work, enter_bootloader);

static int dfu_class_request(struct usb_setup_packet *setup, int32_t *len, uint8_t **data)
{
	if (setup->wIndex != dfu_descriptor.interface.bInterfaceNumber) {
		return -ENOTSUP;
	}

	/* Current Nordic tools send device-recipient requests to interface 0. */
	if (setup->RequestType.recipient != USB_REQTYPE_RECIPIENT_INTERFACE &&
	    (setup->RequestType.recipient != USB_REQTYPE_RECIPIENT_DEVICE || setup->wIndex != 0)) {
		return -ENOTSUP;
	}

	if (usb_reqtype_is_to_device(setup)) {
		/* Older Nordic tools send one data byte with Detach. */
		if (setup->bRequest != DFU_DETACH || !reset_ready || setup->wLength > 1) {
			return -ENOTSUP;
		}
		*len = 0;
		/* Give the USB control transfer time to finish before disconnecting. */
		k_work_schedule(&reset_work, K_MSEC(20));
		return 0;
	}

	if (setup->wValue != 0) {
		return -ENOTSUP;
	}

	switch (setup->bRequest) {
	case DFU_FIRMWARE_INFO:
		*data = (uint8_t *)&firmware_info;
		*len = sizeof(firmware_info);
		return 0;
	case DFU_VERSION_STRING:
		*data = version_string;
		*len = sizeof(version_string);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int dfu_vendor_request(struct usb_setup_packet *setup, int32_t *len, uint8_t **data)
{
	if (setup->bmRequestType != 0xC0 || setup->bRequest != MS_OS_VENDOR_REQUEST ||
	    setup->wIndex != MS_OS_20_DESCRIPTOR_INDEX || setup->wValue != 0) {
		return -ENOTSUP;
	}

	*data = (uint8_t *)&winusb_descriptor;
	*len = sizeof(winusb_descriptor);
	return 0;
}

static void dfu_interface_config(struct usb_desc_header *head, uint8_t interface_number)
{
	ARG_UNUSED(head);
	dfu_descriptor.interface.bInterfaceNumber = interface_number;
	winusb_descriptor.function.bFirstInterface = interface_number;
}

USBD_DEFINE_CFG_DATA(dfu_config) = {
	.interface_config = dfu_interface_config,
	.interface_descriptor = &dfu_descriptor.interface,
	.interface = {
		.class_handler = dfu_class_request,
		.vendor_handler = dfu_vendor_request,
	},
};

static int nordic_dfu_init(void)
{
	static const char interface_guid[] = "{12C68037-030D-4F2A-BABC-89B7F9DCB1AD}";
	int err = gpio_pin_configure_dt(&dfu_reset, GPIO_OUTPUT_INACTIVE);

	reset_ready = err == 0;
	firmware_info.address = sys_cpu_to_le32(CONFIG_FLASH_BASE_ADDRESS + CONFIG_FLASH_LOAD_OFFSET);
	firmware_info.firmware_size = sys_cpu_to_le32((uintptr_t)_flash_used);
	firmware_info.version_major = sys_cpu_to_le16(NORDIC_DFU_VERSION_MAJOR);
	firmware_info.version_minor = sys_cpu_to_le16(NORDIC_DFU_VERSION_MINOR);
	firmware_info.flash_size = sys_cpu_to_le32(NRF_FICR->CODEPAGESIZE * NRF_FICR->CODESIZE);
	firmware_info.flash_page_size = sys_cpu_to_le32(NRF_FICR->CODEPAGESIZE);

	for (size_t i = 0; i < sizeof(interface_guid) - 1; ++i) {
		winusb_descriptor.guid.bPropertyData[2 * i] = interface_guid[i];
	}

	usb_bos_register_cap(&winusb_capability.platform);
	return err;
}

/* Register the descriptors before the HCI transport enables USB at APPLICATION init. */
SYS_INIT(nordic_dfu_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
