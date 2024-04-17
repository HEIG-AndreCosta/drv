#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("REDS");
MODULE_DESCRIPTION("Introduction to the interrupt and platform drivers");

struct data {
	void __iomem *sw;
	void __iomem *leds;
	void __iomem *btn;
	struct device *dev;
};
static struct data *priv;

static irqreturn_t irq_handler(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static int switch_copy_probe(struct platform_device *pdev)
{
	void __iomem *base_pointer;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		return -ENOMEM;
	}
	base_pointer = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base_pointer)) {
		return PTR_ERR(base_pointer);
	}

	priv->leds = base_pointer;
	priv->sw = base_pointer + 0x40;
	priv->btn = base_pointer + 0x50;

	priv->dev = &pdev->dev;

	platform_set_drvdata(pdev, priv);
	return 0;
}

static int switch_copy_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id switch_copy_driver_id[] = {
	{ .compatible = "drv2024" },
	{ /* END */ },
};

MODULE_DEVICE_TABLE(of, switch_copy_driver_id);

static struct platform_driver switch_copy_driver = {
	.driver = {
		.name = "drv-lab4",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(switch_copy_driver_id),
	},
	.probe = switch_copy_probe,
	.remove = switch_copy_remove,
};

module_platform_driver(switch_copy_driver);
