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
#include <asm/io.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("REDS");
MODULE_DESCRIPTION("Introduction to the interrupt and platform drivers");

#define LEDS_OFFSET		  0x00
#define SWITCH_OFFSET		  0x40
#define BTN_DATA_OFFSET		  0x50
#define BTN_EDGE_CAPTURE_OFFSET	  0x5C
#define BTN_INTERRUPT_MASK_OFFSET 0x58

struct data {
	void __iomem *sw;
	void __iomem *leds;
	void __iomem *btn_data;
	void __iomem *btn_interrupt_mask;
	void __iomem *btn_edge_capture;
	struct device *dev;
};

static void rearm_pb_interrupts(struct data *priv)
{
	iowrite8(0x0F, priv->btn_edge_capture);
}

static irqreturn_t irq_handler(int irq, void *dev_id)
{
	struct data *priv = (struct data *)dev_id;
	uint8_t pressed = ioread8(priv->btn_edge_capture);

	(void)irq; // unused

	if (pressed & 0x01) {
		iowrite16(ioread16(priv->sw), priv->leds);
	} else if (pressed & 0x02) {
		iowrite16(ioread16(priv->leds) >> 1, priv->leds);
	}
	rearm_pb_interrupts(priv);

	return IRQ_HANDLED;
}

static int switch_copy_probe(struct platform_device *pdev)
{
	void __iomem *base_pointer;
	struct data *priv;

	// Get the interrupt number
	int btn_interrupt = platform_get_irq(pdev, 0);

	if (btn_interrupt < 0) {
		return btn_interrupt;
	}

	// Allocate memory for our data structure
	priv = devm_kzalloc(&pdev->dev, sizeof(struct data), GFP_KERNEL);
	if (!priv) {
		pr_err("Failed to allocate memory\n");
		return -ENOMEM;
	}

	// Get the base address of the device registers
	base_pointer = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base_pointer)) {
		kfree(priv);
		return PTR_ERR(base_pointer);
	}

	// Request the interrupt. This won't make the interrupt fire yet so it's safe to do it here
	if (devm_request_irq(&pdev->dev, btn_interrupt, irq_handler, 0,
			     "switch_copy", priv) < 0) {
		kfree(priv);
		return -EBUSY;
	}

	// Compute the addresses of the device registers
	priv->leds = base_pointer + LEDS_OFFSET;
	priv->sw = base_pointer + SWITCH_OFFSET;
	priv->btn_data = base_pointer + BTN_DATA_OFFSET;
	priv->btn_interrupt_mask = base_pointer + BTN_INTERRUPT_MASK_OFFSET;
	priv->btn_edge_capture = base_pointer + BTN_EDGE_CAPTURE_OFFSET;
	priv->dev = &pdev->dev;

	// Set the driver data on the platform bus
	platform_set_drvdata(pdev, priv);

	//Enabling interrupts on the hardware
	iowrite8(0xF, priv->btn_interrupt_mask);

	// Arming interrupts
	rearm_pb_interrupts(priv);

	return 0;
}

static int switch_copy_remove(struct platform_device *pdev)
{
	// Get the driver data
	struct data *priv = platform_get_drvdata(pdev);

	if (!priv) {
		pr_err("Failed to get driver data\n");
		return -ENODEV;
	}

	pr_info("Removing driver\n");

	// Disabling interrupts
	iowrite8(0x0, priv->btn_interrupt_mask);

	// Clearing the LEDs
	iowrite16(0x0, priv->leds);

	// Free the memory
	kfree(priv);
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
