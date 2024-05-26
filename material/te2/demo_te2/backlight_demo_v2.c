// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * iPAQ microcontroller backlight support
 * Author : Linus Walleij <linus.walleij@linaro.org>
 */

#include "linux/dev_printk.h"
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct priv {};
static int drv_bl_update_status(struct backlight_device *bd)
{
	// struct priv *priv = (struct priv *)dev_get_drvdata(&bd->dev);
	pr_info("Update Status\n");
	pr_info("\tBrightness: %d\n", backlight_get_brightness(bd));
	pr_info("\tPower State: %d (0: ON 1-3: Low power 4: OFF)\n",
		bd->props.power);
	pr_info("\tBlank: %s\n", backlight_is_blank(bd) ? "YES" : "NO");

	return 0;
}
static int drv_bl_get_brightness(struct backlight_device *bd)
{
	int brightness = backlight_get_brightness(bd);
	pr_info("Get Brightness\n");
	pr_info("\tBrightness: %d\n", brightness);
	if (brightness) {
		return brightness - 1;
	}
	return brightness;
}

static const struct backlight_ops drv_bl_ops = {
	.update_status = drv_bl_update_status,
	.get_brightness = drv_bl_get_brightness,
	.options =
		BL_CORE_SUSPENDRESUME, // Makes kernel call our update status function on suspend/resume
};

static const struct backlight_properties drv_bl_props = {
	.type = BACKLIGHT_RAW,
	.max_brightness = 255,
	.power = FB_BLANK_UNBLANK,
	.brightness = 64,
};

static int drv_bl_probe(struct platform_device *pdev)
{
	struct backlight_device *bd;
	struct priv *priv = dev_get_drvdata(pdev->dev.parent);

	pr_info("Backlight demo\n");
	bd = devm_backlight_device_register(&pdev->dev, "drv-2024", &pdev->dev,
					    priv, &drv_bl_ops, &drv_bl_props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	platform_set_drvdata(pdev, bd);
	backlight_update_status(bd);

	pr_info("Backlight demo probed successfully!\n");

	return 0;
}

static const struct of_device_id driver_id[] = {
	{ .compatible = "drv2024" },
	{ /* END */ },
};

MODULE_DEVICE_TABLE(of, driver_id);

static struct platform_driver drv_bl_device_driver = {
	.driver = {
		.name = "drv-demo-backlight-te2",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(driver_id),
	},
	.probe   = drv_bl_probe,
};

module_platform_driver(drv_bl_device_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Backlight driver demo for TE2 - DRV 2024");
MODULE_AUTHOR("André Costa");
