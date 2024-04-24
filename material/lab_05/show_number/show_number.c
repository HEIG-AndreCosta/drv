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
#include <linux/miscdevice.h>
#include <linux/fs.h> /* Needed for file_operations */
#include <linux/kfifo.h>
#include <linux/kthread.h>
#define DEBUGGING 0

// Define DBG to print only if DEBUGGING is set
#if DEBUGGING
#define DBG(fmt, ...) pr_info(fmt, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif

#define DEVICE_NAME "show_number"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("André Costa");
MODULE_DESCRIPTION(
	"A driver that displays numbers on the de1's 7-segment display");

#define LEDS_OFFSET		  0x00
#define SEVEN_SEG_OFFSET	  0x20
#define SWITCH_OFFSET		  0x40
#define BTN_DATA_OFFSET		  0x50
#define BTN_EDGE_CAPTURE_OFFSET	  0x5C
#define BTN_INTERRUPT_MASK_OFFSET 0x58

#define FIFO_TYPE		  uint32_t
#define FIFO_CAPACITY		  64
#define MAX_VALUE		  999999
struct data {
	void __iomem *sw;
	void __iomem *seven_segment;
	void __iomem *leds;
	void __iomem *btn_data;
	void __iomem *btn_interrupt_mask;
	void __iomem *btn_edge_capture;
	DECLARE_KFIFO(fifo, FIFO_TYPE, FIFO_CAPACITY);
	struct completion zero_found;

	bool stop_thread;
	struct device *dev;
};

static void rearm_pb_interrupts(struct data *priv)
{
	iowrite8(0x0F, priv->btn_edge_capture);
}
static void display_number(void __iomem *seven_segment, uint32_t number)
{
	uint8_t digits[6];
	uint8_t i;
	for (i = 0; i < 6; i++) {
		digits[i] = number % 10;
		number /= 10;
	}
	pr_info("Displaying number %d%d%d%d%d%d\n", digits[5], digits[4],
		digits[3], digits[2], digits[1], digits[0]);

	// iowrite8(digits[0] | (digits[1] << 4), seven_segment);
	// iowrite8(digits[2] | (digits[3] << 4), seven_segment + 1);
	// iowrite8(digits[4] | (digits[5] << 4), seven_segment + 2);
}

static irqreturn_t irq_handler(int irq, void *dev_id)
{
	struct data *priv = (struct data *)dev_id;
	uint8_t pressed = ioread8(priv->btn_edge_capture);

	(void)irq; // unused

	if (pressed & 0x01) {
		priv->stop_thread = true;
	}
	rearm_pb_interrupts(priv);

	return IRQ_HANDLED;
}
static int seven_seg_task(void *arg)
{
	struct data *priv = (struct data *)arg;
	while (!kthread_should_stop()) {
		wait_for_completion(&priv->zero_found);
		while (!kthread_should_stop() && !kfifo_is_empty(&priv->fifo) &&
		       !priv->stop_thread) {
			FIFO_TYPE value;
			if (kfifo_out(&priv->fifo, &value, sizeof(FIFO_TYPE)) !=
			    sizeof(FIFO_TYPE)) {
				continue;
			}
			display_number(priv->seven_segment, value);
		}
	}
	return 0;
}
/**
 * @brief Device file write callback to add a value to the list.
 *
 * @param filp  File structure of the char device to which the value is written.
 * @param buf   Userspace buffer from which the value will be copied.
 * @param count Number of available bytes in the userspace buffer.
 * @param ppos  Current cursor position in the file.
 *
 * @return Number of bytes read from the userspace buffer.
 */
static ssize_t on_write(struct file *filp, const char __user *buf, size_t count,
			loff_t *ppos)
{
	uint8_t *buffer;
	struct data *priv = filp->private_data;
	size_t nb_values = count / sizeof(FIFO_TYPE);

	if (count % sizeof(FIFO_TYPE) != 0 ||
	    kfifo_avail(&priv->fifo) < nb_values) {
		return -EINVAL;
	}

	DBG("Writing %lu values\n", nb_values);

	*ppos = 0;

	buffer = kmalloc(count, GFP_KERNEL);

	if (!buffer) {
		return -ENOMEM;
	}

	if (copy_from_user(buffer, buf, count) != 0) {
		kfree(buffer);
		return -EFAULT;
	}

	for (size_t i = 0; i < nb_values; ++i) {
		FIFO_TYPE value = *(FIFO_TYPE *)&buffer[i * sizeof(FIFO_TYPE)];
		if (value == 0) {
			// Handle the case where the value is 0
			priv->stop_thread = false;
			complete(&priv->zero_found);
			continue;
		} else if (value > MAX_VALUE) {
			//Ignore values that are too big
			continue;
		}
		kfifo_in(&priv->fifo, &value, sizeof(FIFO_TYPE));
	}
	kfree(buffer);
	return count;
}

const static struct file_operations fops = {
	.owner = THIS_MODULE,
	// .read = on_read,
	.write = on_write,
};
static struct miscdevice miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &fops,
};

static int on_probe(struct platform_device *pdev)
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
	priv->seven_segment = base_pointer + SEVEN_SEG_OFFSET;
	priv->sw = base_pointer + SWITCH_OFFSET;
	priv->btn_data = base_pointer + BTN_DATA_OFFSET;
	priv->btn_interrupt_mask = base_pointer + BTN_INTERRUPT_MASK_OFFSET;
	priv->btn_edge_capture = base_pointer + BTN_EDGE_CAPTURE_OFFSET;
	INIT_KFIFO(priv->fifo);
	init_completion(&priv->zero_found);

	priv->dev = &pdev->dev;

	// Set the driver data on the platform bus
	platform_set_drvdata(pdev, priv);

	//Enabling interrupts on the hardware
	iowrite8(0xF, priv->btn_interrupt_mask);

	// Arming interrupts
	rearm_pb_interrupts(priv);
	kthread_run(seven_seg_task, priv, "seven_seg_task");
	return misc_register(&miscdev);
}

static int on_remove(struct platform_device *pdev)
{
	DBG("Removing\n");
	struct data *priv = platform_get_drvdata(pdev);
	kfifo_free(&priv->fifo);

	misc_deregister(&miscdev);
	return 0;
}

// static ssize_t on_read(struct file *filp, char __user *buf, size_t count,
// 		       loff_t *ppos)
// {
// 	struct data *priv = filp->private_data;

// 	if (count < sizeof(FIFO_TYPE)) {
// 		return -EINVAL;
// 	}
// 	if (kfifo_is_empty(&priv->fifo)) {
// 		return 0;
// 	}

// 	FIFO_TYPE value;

// 	if (kfifo_out(&priv->fifo, &value, sizeof(FIFO_TYPE)) !=
// 	    sizeof(FIFO_TYPE)) {
// 		return -EFAULT;
// 	}

// 	if (copy_to_user(buf, &value, sizeof(FIFO_TYPE)) != 0) {
// 		return -EFAULT;
// 	}

// 	*ppos += sizeof(FIFO_TYPE);

// 	return sizeof(FIFO_TYPE);
// }

static const struct of_device_id driver_id[] = {
	{ .compatible = "drv2024" },
	{ /* END */ },
};

MODULE_DEVICE_TABLE(of, driver_id);

static struct platform_driver pf_driver = {
	.driver = {
		.name = "drv-lab5",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(driver_id),
	},
	.probe = on_probe,
	.remove = on_remove,
};

module_platform_driver(pf_driver);
