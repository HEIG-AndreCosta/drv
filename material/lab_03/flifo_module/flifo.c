#include <linux/fs.h> /* Needed for file_operations */
#include <linux/init.h> /* Needed for the macros */
#include <linux/kernel.h> /* Needed for KERN_INFO */
#include <linux/miscdevice.h> /* Needed for misc_register */
#include <linux/module.h> /* Needed by all modules */
#include <linux/slab.h> /* Needed for kmalloc */
#include <linux/string.h>
#include <linux/uaccess.h> /* copy_(to|from)_user */

#include "flifo.h"

#define DEVICE_NAME "flifo"

#define NB_VALUES   16

static int64_t values[NB_VALUES];
static size_t next_in;
static size_t nb_values;
static int mode;

/**
 * @brief Device file read callback to read the value in the list.
 *
 * @param filp  File structure of the char device from which the value is read.
 * @param buf   Userspace buffer to which the value will be copied.
 * @param count Number of available bytes in the userspace buffer.
 * @param ppos  Current cursor position in the file (ignored).
 *
 * @return Number of bytes written in the userspace buffer.
 */
static ssize_t flifo_read(struct file *filp, char __user *buf, size_t count,
			  loff_t *ppos)
{
	typedef union {
		char buffer[8];
		int64_t val64;
	} value_t;

	value_t value;

	if (buf == NULL || nb_values == 0) {
		return 0;
	}

	// Read the value from the list using correct mode.
	switch (mode) {
	case MODE_FIFO:
		value.val64 =
			values[(NB_VALUES + next_in - nb_values) % NB_VALUES];
		break;
	case MODE_LIFO:
		value.val64 = values[next_in - 1];
		break;
	default:
		return 0;
	}

	pr_info("Read value: %lld\n", value.val64);

	// This a simple usage of ppos to avoid infinit loop with `cat`
	// it may not be the correct way to do.
	if (*ppos != 0) {
		return 0;
	}
	*ppos = 0;

	if (copy_to_user(buf, &value.val64, sizeof(value.val64)) != 0) {
		return 0;
	}

	if (mode == MODE_LIFO) {
		next_in--;
	}
	nb_values--;
	pr_info("Read Ok\n");
	return sizeof(value.val64);
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
static ssize_t flifo_write(struct file *filp, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	typedef union {
		char buffer[8];
		int8_t val8;
		int16_t val16;
		int32_t val32;
		int64_t val64;
	} value_t;

	value_t value;
	if (count == 0 || count > 8 || nb_values == NB_VALUES) {
		return 0;
	}

	*ppos = 0;

	// value.buffer = kmalloc(8, GFP_KERNEL);

	// Get the value and convert it to an integer.
	if (copy_from_user(value.buffer, buf, count) != 0) {
		return 0;
	}

	// Add the value in the list.
	switch (count) {
	case 1:
		value.val64 = value.val8;
		break;
	case 2:
		value.val64 = value.val16;
		break;
	case 4:
		value.val64 = value.val32;
		break;
	case 8:
		break;
	default:
		return 0;
	}
	pr_info("Write value: %lld\n", value.val64);
	values[next_in] = value.val64;
	next_in = (next_in + 1) % NB_VALUES;
	nb_values++;

	pr_info("Write Ok\n");
	return count;
}

/**
 * @brief Device file ioctl callback. This permits to modify the behavior of the
 * module.
 *        - If the command is FLIFO_CMD_RESET, then the list is reset.
 *        - If the command is FLIFO_CMD_CHANGE_MODE, then the arguments will
 * determine the list's mode between FIFO (MODE_FIFO) and LIFO (MODE_LIFO)
 *
 * @param filp File structure of the char device to which ioctl is performed.
 * @param cmd  Command value of the ioctl
 * @param arg  Optionnal argument of the ioctl
 *
 * @return 0 if ioctl succeed, -1 otherwise.
 */
static long flifo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FLIFO_CMD_RESET:
		next_in = 0;
		nb_values = 0;
		break;

	case FLIFO_CMD_CHANGE_MODE:
		if (arg != MODE_FIFO && arg != MODE_LIFO) {
			return -1;
		}
		mode = arg;
		break;

	default:
		break;
	}
	return 0;
}

const static struct file_operations flifo_fops = {
	.owner = THIS_MODULE,
	.read = flifo_read,
	.write = flifo_write,
	.unlocked_ioctl = flifo_ioctl,
};
static struct miscdevice flifo_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &flifo_fops,
};
static int __init flifo_init(void)
{
	next_in = 0;
	nb_values = 0;
	mode = MODE_FIFO;

	//	register_chrdev(MAJOR_NUM, DEVICE_NAME, &flifo_fops);
	int ret = misc_register(&flifo_miscdev);
	if (ret) {
		pr_err("misc_register failed\n");
		return ret;
	}
	pr_info("FLIFO ready!\n");
	pr_info("ioctl FLIFO_CMD_RESET: %u\n", FLIFO_CMD_RESET);
	pr_info("ioctl FLIFO_CMD_CHANGE_MODE: %lu\n", FLIFO_CMD_CHANGE_MODE);

	return 0;
}

static void __exit flifo_exit(void)
{
	// unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
	misc_deregister(&flifo_miscdev);
	pr_info("FLIFO done!\n");
}

MODULE_AUTHOR("André Costa");
MODULE_LICENSE("GPL");

module_init(flifo_init);
module_exit(flifo_exit);
