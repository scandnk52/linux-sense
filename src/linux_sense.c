#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/wmi.h>
#include <linux/hwmon.h>
#include <linux/power_supply.h>
#include <linux/acpi.h>

#define WMI_GUID1       "7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56"
#define WMI_GUID2       "79772EC5-04B1-4bfd-843C-61E7F77B6CC9"

#define ACPI_EVENT_DEVICE_ID    "PNP0C14"
#define ACPI_EVENT_TURBO_KEY     0xBC

static const struct acpi_device_id acpi_device_ids[] = {
  { ACPI_EVENT_DEVICE_ID, 0 },
  { "", 0 }
};

static int acpi_event_add(struct acpi_device *device) {
  pr_debug("ACPI device loaded: %s\n", acpi_device_hid(device));
  return 0;
}

static void acpi_event_remove(struct acpi_device *device) {
  pr_debug("ACPI device removed: %s\n", acpi_device_hid(device));
}

static void acpi_event_notify(struct acpi_device *device, const u32 event) {
  if (event == ACPI_EVENT_TURBO_KEY) {
    // thermal_profile_change();
  }
}

static struct acpi_driver acpi_event_driver = {
  .name = "linux_sense_acpi",
  .class = "linux_sense",
  .ids = acpi_device_ids,
  .ops = {
      .add = acpi_event_add,
      .remove = acpi_event_remove,
      .notify = acpi_event_notify,
  },
};

static int __init acpi_event_init(void) {
  const int ret = acpi_bus_register_driver(&acpi_event_driver);
  if (ret < 0) {
    pr_err("Failed to register the ACPI device\n");
    return ret;
  }
  return 0;
}

static void __exit acpi_event_destroy(void) {
  acpi_bus_unregister_driver(&acpi_event_driver);
}

static struct platform_device *platform_device;

static enum power_supply_property power_props[] = {
  POWER_SUPPLY_PROP_CHARGE_TYPE,
  POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
  POWER_SUPPLY_PROP_CALIBRATE,
};

struct power_data {
  struct power_supply *psy;
  int usb_power_off_charge;
  bool charge_mode;
  bool calibration_mode;
};

static int supply_get_property(struct power_supply *psy, enum power_supply_property psp, union power_supply_propval *val) {
  const struct power_data *data = power_supply_get_drvdata(psy);

  switch (psp) {
    case POWER_SUPPLY_PROP_CHARGE_TYPE:
      val->intval = data->usb_power_off_charge;
      break;
    case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
      val->intval = data->charge_mode;
      break;
    case POWER_SUPPLY_PROP_CALIBRATE:
      val->intval = data->calibration_mode;
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

static int supply_set_property(struct power_supply *psy, enum power_supply_property psp, const union power_supply_propval *val) {
  struct power_data *data = power_supply_get_drvdata(psy);

  switch (psp) {
    case POWER_SUPPLY_PROP_CHARGE_TYPE:
      if (val->intval == 0 || val->intval == 10 || val->intval == 20 || val->intval == 30) {
        data->usb_power_off_charge = val->intval;
      } else {
        return -EINVAL;
      }
    break;
    case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
      data->charge_mode = !!val->intval;
    break;
    case POWER_SUPPLY_PROP_CALIBRATE:
      data->calibration_mode = !!val->intval;
    break;
    default:
      return -EINVAL;
  }

  // WMI Battery Settings

  return 0;
}

static struct power_supply_desc power_supply_desc = {
  .name = "linux_sense",
  .type = POWER_SUPPLY_TYPE_BATTERY,
  .properties = power_props,
  .num_properties = ARRAY_SIZE(power_props),
  .get_property = supply_get_property,
  .set_property = supply_set_property,
};

static int power_supply_init(struct platform_device *device) {
  struct power_supply_config psy_cfg = {};
  struct power_data *data;

  data = devm_kzalloc(&device->dev, sizeof(*data), GFP_KERNEL);
  if (!data)
    return -ENOMEM;

  // for now
  data->usb_power_off_charge = 0x00;
  data->charge_mode = false;
  data->calibration_mode = false;

  psy_cfg.drv_data = data;

  data->psy = devm_power_supply_register(&device->dev, &power_supply_desc, &psy_cfg);
  if (IS_ERR(data->psy)) {
    const long ret = PTR_ERR(data->psy);
    pr_err("Failed to register power supply\n");
    return (int)ret;
  }

  platform_set_drvdata(device, data);
  pr_info("Power supply registered\n");

  return 0;
}

static void power_supply_destroy(struct platform_device *device) {
  pr_info("Power supply removed\n");
}

static int platform_probe(struct platform_device *device) {
  int err;

  err = power_supply_init(device);
  if (err)
    power_supply_destroy(device);

  return 0;
}

static void platform_remove(struct platform_device *device) {
  power_supply_destroy(device);
}

static struct platform_driver platform_driver = {
  .probe = platform_probe,
  .remove = platform_remove,
  .driver = {
    .name = "linux_sense",
  },
};

static int __init wmi_init(void) {
  if (!wmi_has_guid(WMI_GUID1) && !wmi_has_guid(WMI_GUID2)) {
    pr_err("WMI GUIDs not found\n");
    return -ENODEV;
  }

  int err = acpi_event_init();
  if (err)
    return err;

  err = platform_driver_register(&platform_driver);
  if (err) {
    pr_err("Failed to register platform driver\n");
    acpi_event_destroy();
    return err;
  }

  platform_device = platform_device_alloc("linux_sense", PLATFORM_DEVID_NONE);
  if (!platform_device) {
    pr_err("Failed to allocate platform device\n");
    platform_driver_unregister(&platform_driver);
    acpi_event_destroy();
    return -ENOMEM;
  }

  err = platform_device_add(platform_device);
  if (err) {
    pr_err("Failed to add platform device\n");
    platform_device_put(platform_device);
    platform_driver_unregister(&platform_driver);
    acpi_event_destroy();
    return err;
  }

  pr_info("Driver loaded\n");
  return 0;
}

static void __exit wmi_exit(void) {
  acpi_event_destroy();

  platform_device_unregister(platform_device);
  platform_driver_unregister(&platform_driver);

  pr_info("Driver unloaded\n");
}

MODULE_DESCRIPTION("Linux driver for Acer Nitro laptop series");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("override");

MODULE_ALIAS("wmi:" WMI_GUID1);
MODULE_ALIAS("wmi:" WMI_GUID2);

module_init(wmi_init);
module_exit(wmi_exit);
