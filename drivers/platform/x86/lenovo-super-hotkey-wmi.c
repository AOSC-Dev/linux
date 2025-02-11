// SPDX-License-Identifier: GPL-2.0
/*
 *  Lenovo Super Hotkey Utility WMI extras driver for Ideapad laptop
 *
 *  Copyright (C) 2025	Lenovo
 */

#include <linux/leds.h>
#include <linux/wmi.h>
#include "ideapad-laptop.h"

/* Lenovo Super Hotkey WMI GUIDs */
#define LUD_WMI_METHOD_GUID	"CE6C0974-0407-4F50-88BA-4FC3B6559AD8"

/* Lenovo Utility Data WMI method_id */
#define WMI_LUD_GET_SUPPORT 1
#define WMI_LUD_SET_FEATURE 2

#define WMI_LUD_GET_MICMUTE_LED_VER   20
#define WMI_LUD_GET_AUDIOMUTE_LED_VER 26

#define WMI_LUD_SUPPORT_MICMUTE_LED_VER   25
#define WMI_LUD_SUPPORT_AUDIOMUTE_LED_VER 27

/* Input parameters to mute/unmute audio LED and Mic LED */
struct wmi_led_args {
	u8 id;
	u8 subid;
	u16 value;
};

/* Values of input parameters to SetFeature of audio LED and Mic LED */
enum hotkey_set_feature {
	MIC_MUTE_LED_ON = 1,
	MIC_MUTE_LED_OFF,
	AUDIO_MUTE_LED_ON = 4,
	AUDIO_MUTE_LED_OFF,
};

#define LSH_ACPI_LED_MAX 2

enum lenovo_super_hotkey_wmi_event_type {
	LSH_WMI_EVENT_LUD_KEYS = 2,
};

struct lenovo_super_hotkey_wmi_private {
	enum lenovo_super_hotkey_wmi_event_type event;
	struct led_classdev cdev[LSH_ACPI_LED_MAX];
	struct wmi_device *led_wdev;
};

enum mute_led_type {
	MIC_MUTE,
	AUDIO_MUTE,
};

static int lsh_wmi_mute_led_set(enum mute_led_type led_type, struct led_classdev *led_cdev,
				enum led_brightness brightness)

{
	struct lenovo_super_hotkey_wmi_private *wpriv = container_of(led_cdev,
			struct lenovo_super_hotkey_wmi_private, cdev[led_type]);
	struct wmi_led_args led_arg = {0, 0, 0};
	struct acpi_buffer input;
	acpi_status status;

	switch (led_type) {
	case MIC_MUTE:
		led_arg.id = brightness == LED_ON ? MIC_MUTE_LED_ON : MIC_MUTE_LED_OFF;
		break;
	case AUDIO_MUTE:
		led_arg.id = brightness == LED_ON ? AUDIO_MUTE_LED_ON : AUDIO_MUTE_LED_OFF;
		break;
	default:
		return -EINVAL;
	}

	input.length = sizeof(led_arg);
	input.pointer = &led_arg;
	status = wmidev_evaluate_method(wpriv->led_wdev, 0, WMI_LUD_SET_FEATURE, &input, NULL);
	if (ACPI_FAILURE(status))
		return -EIO;

	return 0;
}

static int lsh_wmi_audiomute_led_set(struct led_classdev *led_cdev,
				     enum led_brightness brightness)

{
	return lsh_wmi_mute_led_set(AUDIO_MUTE, led_cdev, brightness);
}

static int lsh_wmi_micmute_led_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	return lsh_wmi_mute_led_set(MIC_MUTE, led_cdev, brightness);
}

static int lenovo_super_hotkey_wmi_led_init(enum mute_led_type led_type, struct device *dev)
{
	struct lenovo_super_hotkey_wmi_private *wpriv = dev_get_drvdata(dev);
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer input;
	union acpi_object *obj;
	int led_version, err = 0;
	unsigned int wmiarg;
	acpi_status status;

	switch (led_type) {
	case MIC_MUTE:
		wmiarg = WMI_LUD_GET_MICMUTE_LED_VER;
		break;
	case AUDIO_MUTE:
		wmiarg = WMI_LUD_GET_AUDIOMUTE_LED_VER;
		break;
	default:
		return -EINVAL;
	}

	input.length = sizeof(wmiarg);
	input.pointer = &wmiarg;
	status = wmidev_evaluate_method(wpriv->led_wdev, 0, WMI_LUD_GET_SUPPORT, &input, &output);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = output.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER) {
		led_version = obj->integer.value;
		kfree(output.pointer);
	} else {
		err = -EIO;
		goto led_error;
	}

	wpriv->cdev[led_type].max_brightness = LED_ON;
	wpriv->cdev[led_type].dev = dev;
	wpriv->cdev[led_type].flags = LED_CORE_SUSPENDRESUME;

	switch (led_type) {
	case MIC_MUTE:
		if (led_version != WMI_LUD_SUPPORT_MICMUTE_LED_VER) {
			err = -EIO;
			goto led_error;
		}
		wpriv->cdev[led_type].name = "platform::micmute";
		wpriv->cdev[led_type].brightness_set_blocking = &lsh_wmi_micmute_led_set;
		wpriv->cdev[led_type].default_trigger = "audio-micmute";

		err = devm_led_classdev_register(dev, &wpriv->cdev[led_type]);
		if (err < 0)
			goto led_reg_failed;

		break;
	case AUDIO_MUTE:
		if (led_version != WMI_LUD_SUPPORT_AUDIOMUTE_LED_VER) {
			err = -EIO;
			goto led_error;
		}
		wpriv->cdev[led_type].name = "platform::mute";
		wpriv->cdev[led_type].brightness_set_blocking = &lsh_wmi_audiomute_led_set;
		wpriv->cdev[led_type].default_trigger = "audio-mute";

		err = devm_led_classdev_register(dev, &wpriv->cdev[led_type]);
		if (err < 0)
			goto led_reg_failed;

		break;
	default:
		err = -EINVAL;
		dev_err(dev, "Unknown LED type %d\n", led_type);
		goto led_error;
	}

	return 0;

led_reg_failed:
	dev_err(dev, "Could not register mute LED %d : %d\n", led_type, err);

led_error:
	kfree(obj);
	return err;
}

static void lenovo_super_hotkey_wmi_leds_setup(struct device *dev)
{
	lenovo_super_hotkey_wmi_led_init(MIC_MUTE, dev);
	lenovo_super_hotkey_wmi_led_init(AUDIO_MUTE, dev);
}

static int lenovo_super_hotkey_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct lenovo_super_hotkey_wmi_private *wpriv;

	wpriv = devm_kzalloc(&wdev->dev, sizeof(*wpriv), GFP_KERNEL);
	if (!wpriv)
		return -ENOMEM;

	*wpriv = *(const struct lenovo_super_hotkey_wmi_private *)context;

	dev_set_drvdata(&wdev->dev, wpriv);

	if (wpriv->event == LSH_WMI_EVENT_LUD_KEYS) {
		wpriv->led_wdev = wdev;
		lenovo_super_hotkey_wmi_leds_setup(&wdev->dev);
	}

	return 0;
}

static void lenovo_super_hotkey_wmi_remove(struct wmi_device *wdev)
{
	struct lenovo_super_hotkey_wmi_private *wpriv = dev_get_drvdata(&wdev->dev);
	unsigned int i;

	for (i = 0; i < LSH_ACPI_LED_MAX; i++)
		led_classdev_unregister(&wpriv->cdev[i]);

	kfree(wpriv);
}
static const struct lenovo_super_hotkey_wmi_private lsk_wmi_context_lud_keys = {
	.event = LSH_WMI_EVENT_LUD_KEYS
};

static const struct wmi_device_id lenovo_super_hotkey_wmi_id_table[] = {
	{ LUD_WMI_METHOD_GUID, &lsk_wmi_context_lud_keys }, /* Utility data */
	{ }
};

MODULE_DEVICE_TABLE(wmi, lenovo_super_hotkey_wmi_id_table);

static struct wmi_driver lenovo_super_hotkey_wmi_driver = {
	 .driver = {
		 .name = "lenovo_super_hotkey_wmi",
		 .probe_type = PROBE_PREFER_ASYNCHRONOUS
	 },
	 .id_table = lenovo_super_hotkey_wmi_id_table,
	 .probe = lenovo_super_hotkey_wmi_probe,
	 .remove = lenovo_super_hotkey_wmi_remove,
	 .no_singleton = true,
};

module_wmi_driver(lenovo_super_hotkey_wmi_driver);

MODULE_INFO(depends, "wmi,ideapad-laptop");
MODULE_AUTHOR("Jackie Dong <dongeg1@lenovo.com>");
MODULE_DESCRIPTION("Lenovo Super Hotkey Utility WMI extras driver");
MODULE_LICENSE("GPL");
