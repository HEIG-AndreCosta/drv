#include <linux/module.h> /* Needed by all modules */
#include <linux/kernel.h> /* Needed for KERN_INFO */
#include <linux/init.h> /* Needed for the macros */
#include <linux/fs.h> /* Needed for file_operations */
#include <linux/slab.h> /* Needed for kmalloc */
#include <linux/uaccess.h> /* copy_(to|from)_user */

#include <linux/string.h>

#include "flifo.h"

#define MAJOR_NUM	      97
#define DEVICE_NAME	      "flifo"

#define NB_VALUES	      16

static int values[NB_VALUES];
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
	char buffer[256];
	int nb_char;
	int value;

	if (buf == NULL) {
		return 0;
	}

	// Read the value from the list using correct mode.
	switch (mode) {
	case MODE_FIFO:
		value = values[(NB_VALUES + next_in - nb_values) % NB_VALUES];
		break;
	case MODE_LIFO:
		value = values[next_in - 1];
		break;
	default:
		return 0;
	}

	// Convert the integer to a string
	nb_char = snprintf(buffer, sizeof(buffer), "%d", value);

	// This a simple usage of ppos to avoid infinit loop with `cat`
	// it may not be the correct way to do.
	if (*ppos != 0) {
		return 0;
	}
	*ppos = nb_char;

	copy_to_user(buf, buffer, nb_char);

	if (mode == MODE_LIFO) {
		next_in--;
	}
	nb_values--;

	return nb_char;
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
	char *buffer;
	uint32_t value;

	if (count == 0) {
		return 0;
	}

	*ppos = 0;

	buffer = kmalloc(count + 1, GFP_KERNEL);

	// Get the value and convert it to an integer.
	copy_from_user(buffer, buf, count);
	sscanf(buffer, "%d", &value);

	// Add the value in the list.
	values[next_in] = value;
	next_in = (next_in + 1) % NB_VALUES;
	nb_values++;

	kfree(buffer);

	return count;
}

/**
 * @brief Device file ioctl callback. This permits to modify the behavior of the module.
 *        - If the command is FLIFO_CMD_RESET, then the list is reset.
 *        - If the command is FLIFO_CMD_CHANGE_MODE, then the arguments will determine
 *          the list's mode between FIFO (MODE_FIFO) and LIFO (MODE_LIFO)
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

static int __init flifo_init(void)
{
	next_in = 0;
	nb_values = 0;
	mode = MODE_FIFO;

	register_chrdev(MAJOR_NUM, DEVICE_NAME, &flifo_fops);

	pr_info("FLIFO ready!\n");
	pr_info("ioctl FLIFO_CMD_RESET: %u\n", FLIFO_CMD_RESET);
	pr_info("ioctl FLIFO_CMD_CHANGE_MODE: %lu\n", FLIFO_CMD_CHANGE_MODE);

	return 0;
}

static void __exit flifo_exit(void)
{
	unregister_chrdev(MAJOR_NUM, DEVICE_NAME);

	pr_info("FLIFO done!\n");
}

MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

module_init(flifo_init);
module_exit(flifo_exit);
