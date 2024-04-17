// SPDX-License-Identifier: GPL-2.0
/*
 * Parrot file
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>

#include <linux/string.h>

#define START_BUFFER_CAPACITY 8
#define MAX_BUFFER_CAPACITY   1024

#define MAJOR_NUM	      98
#define MAJMIN		      MKDEV(MAJOR_NUM, 0)
#define DEVICE_NAME	      "parrot"

static struct cdev cdev;
static struct class *cl;
struct buffer {
	uint8_t *data;
	size_t size;
	size_t capacity;
};

static struct buffer buffer;

/**
 * @brief Read back previously written data in the internal buffer.
 *
 * @param filp pointer to the file descriptor in use
 * @param buf destination buffer in user space
 * @param count maximum number of data to read
 * @param ppos current position in file from which data will be read
 *              will be updated to new location
 *
 * @return Actual number of bytes read from internal buffer,
 *         or a negative error code
 */
static ssize_t parrot_read(struct file *filp, char __user *buf, size_t count,
			   loff_t *ppos)
{
	if (*ppos >= buffer.size) {
		return 0;
	}
	if (*ppos + count > buffer.size) {
		count = buffer.size - *ppos;
	}
	if (copy_to_user(buf, buffer.data + *ppos, count) != 0) {
		return -EFAULT;
	}
	*ppos += count;

	return count;
}

/**
 * @brief Write data to the internal buffer
 *
 * @param filp pointer to the file descriptor in use
 * @param buf source buffer in user space
 * @param count number of data to write in the buffer
 * @param ppos current position in file to which data will be written
 *              will be updated to new location
 *
 * @return Actual number of bytes writen to internal buffer,
 *         or a negative error code
 */
static ssize_t parrot_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	if (*ppos + count >= buffer.capacity) {
		size_t new_capacity = buffer.capacity;
		uint8_t *new_buffer = NULL;

		if (buffer.capacity == MAX_BUFFER_CAPACITY) {
			pr_info("Buffer at max capacity\n");
			return -ENOMEM;
		}
		new_capacity = buffer.capacity;

		do {
			new_capacity *= 2;
		} while (new_capacity < *ppos + count);

		new_buffer = krealloc(buffer.data, new_capacity, GFP_KERNEL);

		if (!new_buffer) {
			pr_err("Error reallocating buffer\n");
			return -ENOMEM;
		}
		buffer.data = new_buffer;
		buffer.capacity = new_capacity;
	}

	if (copy_from_user(buffer.data + *ppos, buf, count) != 0) {
		return -EFAULT;
	}
	buffer.size += count;
	*ppos += count;

	return count;
}

/**
 * @brief uevent callback to set the permission on the device file
 *
 * @param dev pointer to the device
 * @param env ueven environnement corresponding to the device
 */
static int parrot_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	// Set the permissions of the device file
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}

static const struct file_operations parrot_fops = {
	.owner = THIS_MODULE,
	.read = parrot_read,
	.write = parrot_write,
	.llseek = default_llseek, // Use default to enable seeking to 0
};

static int __init parrot_init(void)
{
	int err;

	// Register the device
	err = register_chrdev_region(MAJMIN, 1, DEVICE_NAME);
	if (err != 0) {
		pr_err("Parrot: Registering char device failed\n");
		return err;
	}

	cl = class_create(THIS_MODULE, DEVICE_NAME);
	if (cl == NULL) {
		pr_err("Parrot: Error creating class\n");
		err = -1;
		goto err_class_create;
	}
	cl->dev_uevent = parrot_uevent;

	if (device_create(cl, NULL, MAJMIN, NULL, DEVICE_NAME) == NULL) {
		pr_err("Parrot: Error creating device\n");
		err = -1;
		goto err_device_create;
	}

	cdev_init(&cdev, &parrot_fops);
	err = cdev_add(&cdev, MAJMIN, 1);
	if (err < 0) {
		pr_err("Parrot: Adding char device failed\n");
		goto err_cdev_add;
	}
	buffer.data = kmalloc(START_BUFFER_CAPACITY, GFP_KERNEL);

	if (!buffer.data) {
		pr_err("Parrot: Error allocating buffer\n");
		err = -ENOMEM;
		goto err_buffer_create;
	}
	buffer.size = 0;
	buffer.capacity = START_BUFFER_CAPACITY;

	pr_info("Parrot ready!\n");

	return 0;

err_buffer_create:
	kfree(buffer.data);
err_cdev_add:
	device_destroy(cl, MAJMIN);
err_device_create:
	class_destroy(cl);
err_class_create:
	unregister_chrdev_region(MAJMIN, 1);
	return err;
}

static void __exit parrot_exit(void)
{
	// Unregister the device
	cdev_del(&cdev);
	device_destroy(cl, MAJMIN);
	class_destroy(cl);
	unregister_chrdev_region(MAJMIN, 1);
	kfree(buffer.data);
	pr_info("Parrot done!\n");
}

MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

module_init(parrot_init);
module_exit(parrot_exit);
