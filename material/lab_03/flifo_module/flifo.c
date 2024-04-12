#include <linux/fs.h> /* Needed for file_operations */
#include <linux/init.h> /* Needed for the macros */
#include <linux/kernel.h> /* Needed for KERN_INFO */
#include <linux/miscdevice.h> /* Needed for misc_register */
#include <linux/module.h> /* Needed by all modules */
#include <linux/slab.h> /* Needed for kmalloc */
#include <linux/string.h>
#include <linux/uaccess.h> /* copy_(to|from)_user */

#include "flifo.h"

#define DEBUGGING 0

// Define DBG to print only if DEBUGGING is set
#if DEBUGGING
#define DBG(fmt, ...) pr_info(fmt, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif

#define DEVICE_NAME "flifo"

#define NB_VALUES   64

static size_t value_size; // Size of the integer value
static size_t value_count; // Number of values in the list
static uint8_t values[NB_VALUES]; // List of values

static size_t next_in; // Next position to write in the list
static int mode; // Mode of the list (FIFO or LIFO)

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
	uint8_t *buffer;
	if (buf == NULL || count % value_size != 0 || count > value_count) {
		return 0;
	}
	DBG("Reading %lu values\n", count / value_size);
	//create a temporary buffer to stock the data
	buffer = kmalloc(count, GFP_KERNEL);

	if (!buffer) {
		return 0;
	}

	// This a simple usage of ppos to avoid infinit loop with `cat`
	// it may not be the correct way to do.
	if (*ppos != 0) {
		return 0;
	}
	*ppos = 0;

	// Read the values from the list 1 byte at a time
	// Can't use memcpy because may not be contiguous depending
	// on where we are on our circular buffer
	// Also we can't use memcpy if we are in LIFO mode
	for (size_t i = 0; i < count; ++i) {
		uint8_t value;
		// Read the value from the list using correct mode.
		switch (mode) {
		case MODE_FIFO:
			value = values[(NB_VALUES + next_in - value_count) %
				       NB_VALUES];
			break;
		case MODE_LIFO:
			value = values[next_in - 1];
			break;
		default:
			return 0;
		}
		// Store the value in our buffer
		buffer[i] = value;

		// Update the next_in and value_count
		--value_count;
		if (mode == MODE_LIFO) {
			--next_in;
		}
	}

	for (size_t i = 0; i < count; i++) {
		DBG("Buffer[%lu] = %u\n", i, buffer[i]);
	}
	// Copy our buffer to the user space buffer
	if (copy_to_user(buf, buffer, count) != 0) {
		kfree(buffer);
		return 0;
	}
	DBG("Read Ok, next_in: %lu\n", next_in);
	kfree(buffer);
	return count;
}
/**
 * @brief  writes the values in buffer to our dst depending on the mode
 * This function updates next_in and value_count
 * @param buffer the buffer containing the values to write
 * @param count  the buffer size
 * @param dst    the destination buffer
 * @param size the size of the values to write
 */
static void write_to_list(uint8_t *buffer, size_t count, uint8_t *dst,
			  size_t size)
{
	const size_t nb_values = count / size;
	switch (mode) {
	case MODE_FIFO:
		for (size_t i = 0; i < count; i++) {
			dst[next_in] = buffer[i];
			next_in = (next_in + 1) % NB_VALUES;
			value_count++;
		}
		break;
	case MODE_LIFO:
		//Here we need to write each value in reverse order
		// so that when we read them back we get the original order
		//eg: size = 2 and user sends us 0x01 0x02
		//we need to store them as 0x10 0x20 so that
		// when we read the bytes back we retrieve 0x02 0x01 (lifo order)
		DBG("Count: %lu\n", count);
		DBG("Size: %lu\n", size);
		DBG("Nb_values: %lu\n", nb_values);
		for (size_t i = 0; i < nb_values; ++i) {
			for (size_t j = size; j > 0; --j) {
				size_t src_index = i * size + j - 1;
				dst[next_in] = buffer[src_index];
				next_in = (next_in + 1) % NB_VALUES;
				value_count++;
			}
		}
	default:
		break;
	}
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
	uint8_t *buffer;
	if (count == 0 || count % value_size != 0 ||
	    value_count + count > NB_VALUES) {
		return 0;
	}
	DBG("Writing %lu values\n", count / value_size);
	*ppos = 0;

	buffer = kmalloc(count, GFP_KERNEL);

	if (!buffer) {
		return 0;
	}

	// Get the value and convert it to an integer.
	if (copy_from_user(buffer, buf, count) != 0) {
		kfree(buffer);
		return 0;
	}
	for (size_t i = 0; i < count; i++) {
		DBG("Buffer[%lu] = %u\n", i, buffer[i]);
	}
	write_to_list(buffer, count, values, value_size);

	DBG("Write Ok, next_id %lu\n", next_in);
	kfree(buffer);
	return count;
}

/**
 * @brief Checks if the integer value is valid 
 * Size should be 1, 2, 4 or 8
 * @param arg the argument to check
 * @return 1 if size is valid, 0 otherwise 
 */
static int is_size_valid(unsigned long arg)
{
	int possible_values[] = { 1, 2, 4, 8 };
	ssize_t size = sizeof(possible_values) / sizeof(possible_values[0]);
	for (int i = 0; i < size; ++i) {
		if (possible_values[i] == arg) {
			return 1;
		}
	}
	return 0;
}
/**
 * @brief Resets the list 
 * 
 */
static void reset_list(void)
{
	next_in = 0;
	value_count = 0;
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
		reset_list();
		break;

	case FLIFO_CMD_CHANGE_MODE:
		if (arg != MODE_FIFO && arg != MODE_LIFO) {
			return -1;
		}
		mode = arg;
		pr_info("Resetting list\n");
		reset_list();
		break;
	case FLIFO_CMD_CHANGE_VALUE_SIZE:
		if (!is_size_valid(arg)) {
			pr_err("Invalid value size\n");
			return -1;
		}
		value_size = arg;
		DBG("Value size changed to %lu\n", value_size);
		pr_info("Resetting list\n");
		reset_list();
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
	int ret;
	next_in = 0;
	value_count = 0;
	value_size = 1;
	mode = MODE_FIFO;

	//	register_chrdev(MAJOR_NUM, DEVICE_NAME, &flifo_fops);
	ret = misc_register(&flifo_miscdev);
	if (ret) {
		pr_err("misc_register failed\n");
		return ret;
	}
	pr_info("FLIFO ready!\n");
	pr_info("ioctl FLIFO_CMD_RESET: %u\n", FLIFO_CMD_RESET);
	pr_info("ioctl FLIFO_CMD_CHANGE_MODE: %zu\n", FLIFO_CMD_CHANGE_MODE);
	pr_info("ioctl FLIFO_CMD_CHANGE_VALUE_SIZE: %zu\n",
		FLIFO_CMD_CHANGE_VALUE_SIZE);
	pr_info("Current integer size: %zu\n", value_size);

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
