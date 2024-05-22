#include "linux/jiffies.h"
#include "linux/spinlock.h"
#include "linux/types.h"
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("André Costa");
MODULE_DESCRIPTION("Chronometre");

#define LEDS_OFST	  0x0
#define KEY_OFST	  0x50
#define KEY_IRQ_EN_OFST	  (KEY_OFST + 0x8)
#define KEY_IRQ_EDGE_OFST (KEY_OFST + 0xC)
#define SWITCH_OFFSET	  (0x40)

#define NB_LEDS		  10
#define LEDS_MASK	  ((1 << NB_LEDS) - 1)
#define NB_SWITCH	  10
#define SWITCH_MASK	  ((1 << NB_SWITCH) - 1)

#define DEV_NAME	  "chronometre"

/**
 * struct priv - Private data for the device
 * @mem_ptr:	Pointer to the IO mapped memory.
 * @dev:	Pointer to the device.
 * @value:	Actual value displayed on the leds.
 * @mod:	Actual mod used to modify the value.
 * @work:	Delayed work used to update the value.
 */
enum chronometre_state { CHRONO_STOP, CHRONO_RUN };
enum chronometre_display_state { CHRONO_DISPLAY_TIME, CHRONO_DISPLAY_LAP };
enum chronometre_lap_display_type {
	CHRONO_LAP_DISPLAY_FROM_START,
	CHRONO_LAP_DISPLAY_SINCE_LAST_LAP,
	CHRONO_LAP_DISPLAY_LEN
};
struct display_time {
	uint32_t minutes;
	uint32_t seconds;
	uint32_t millis;
};
struct lap_time {
	struct list_head list;
	uint64_t total_jiffies;
};
struct chronometre {
	void *mem_ptr;
	struct device *dev;
	struct list_head lap_times;
	size_t lap_times_size;

	struct delayed_work work;
	spinlock_t sp;
	enum chronometre_state state;
	enum chronometre_display_state display_state;
	enum chronometre_lap_display_type lap_display_type;
	uint64_t start_jiffies;
};

#define MS_IN_A_MINUTE (1000 * 60) //1000 second
#define MS_IN_A_SECOND (1000)

static void get_display_time(struct display_time *out_display_time,
			     uint64_t start_jiffies, uint64_t end_jiffies)
{
	uint32_t t = jiffies_delta_to_msecs(end_jiffies - start_jiffies);

	out_display_time->minutes = t / MS_IN_A_MINUTE;
	t %= MS_IN_A_MINUTE;
	out_display_time->seconds = t / MS_IN_A_SECOND;
	t %= MS_IN_A_SECOND;
	out_display_time->millis = t;
}
static void get_current_display_time(struct chronometre *chrono,
				     struct display_time *out_display_time)
{
	if (chrono->state != CHRONO_RUN) {
		out_display_time->minutes = 0;
		out_display_time->seconds = 0;
		out_display_time->millis = 0;
		return;
	}
	return get_display_time(out_display_time, chrono->start_jiffies,
				get_jiffies_64());
}

/* Prototypes for sysfs callbacks */
static ssize_t is_running_show(struct device *dev,
			       struct device_attribute *attr, char *buf);

static ssize_t time_show(struct device *dev, struct device_attribute *attr,
			 char *buf);
static ssize_t nb_laps_show(struct device *dev, struct device_attribute *attr,
			    char *buf);

static ssize_t lap_display_type_show(struct device *dev,
				     struct device_attribute *attr, char *buf);

static ssize_t lap_display_type_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count);

static DEVICE_ATTR_RO(is_running);
static DEVICE_ATTR_RO(time);
static DEVICE_ATTR_RO(nb_laps);

static DEVICE_ATTR_RW(lap_display_type);

static struct attribute *lc_attrs[] = {
	&dev_attr_is_running.attr,
	&dev_attr_time.attr,
	&dev_attr_nb_laps.attr,
	&dev_attr_lap_display_type.attr,
	NULL,
};

static struct attribute_group lc_attr_group = {
	.name = "config",
	.attrs = lc_attrs,
};

/**
 * lc_write - Write a value to a register in the IO mapped memory.
 * @priv:	Pointer to the private data of the device.
 * @reg_offset:	Offset of the register to write (in bytes).
 * @value:	Value to write to the register.
 */
static void lc_write(const struct chronometre *const priv, const int reg_offset,
		     const uint32_t value)
{
	// Assert that the pointers are not NULL, this should never happend
	WARN_ON(priv == NULL);
	WARN_ON(priv->mem_ptr == NULL);

	iowrite32(value,
		  (uint32_t *)priv->mem_ptr + reg_offset / sizeof(uint32_t));
}

static ssize_t lap_display_type_show(struct device *dev,
				     struct device_attribute *attr, char *buf);

static ssize_t lap_display_type_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count);
/**
 * is_running_show - Callback to show if the chrono is currently running
 *
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer to write the read data to.
 * Return: The number of bytes written to the buffer.
 */
static ssize_t is_running_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct chronometre *priv = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n",
			  priv->state == CHRONO_RUN ? "RUNNING" : "IDLE");
}
/**
 * nb_laps_show - Callback to show the amount of laps stored
 *
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer to write the read data to.
 * Return: The number of bytes written to the buffer.
 */
static ssize_t nb_laps_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct chronometre *priv = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%zu\n", priv->lap_times_size);
}

/**
 * time_show - Callback to show the current chrono time
 *
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer to write the read data to.
 * Return: The number of bytes written to the buffer.
 */
static ssize_t time_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct chronometre *priv = dev_get_drvdata(dev);
	struct display_time time;
	get_current_display_time(priv, &time);

	return sysfs_emit(buf, "%2d:%2d:%2d\n", time.minutes, time.seconds,
			  time.millis);
}
/**
 * lap_display_type_show - Callback to show the current lap display type
 *
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer to write the read data to.
 * Return: The number of bytes written to the buffer.
 */
static ssize_t lap_display_type_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct chronometre *priv = dev_get_drvdata(dev);
	struct display_time time;
	get_current_display_time(priv, &time);

	return sysfs_emit(
		buf,
		"(0): Display Time Since Start, (1) Display Time Since Last Lap. Current : %d\n",
		priv->lap_display_type ==
			CHRONO_LAP_DISPLAY_SINCE_LAST_LAP); // if true we'll display 1
}

/**
 * lap_displa_type_store - Callback to change the lap display time.
 * 0 -> Display time since start
 * 1 -> Display time since last lap
 *
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer to read the data from.
 * @count:	Number of bytes to read.
 * Return: The number of bytes read from the buffer.
 */
static ssize_t mod_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct chronometre *priv = dev_get_drvdata(dev);
	int rc;
	uint8_t new_type;

	rc = kstrtou8(buf, 10, &new_type);
	if (rc != 0) {
		dev_err(dev, "Failed to convert the value to an integer !\n");
		return rc;
	}

	if (new_type >= CHRONO_LAP_DISPLAY_LEN) {
		return -EINVAL;
	}

	priv->lap_display_type = (enum chronometre_lap_display_type)new_type;
	return count;
}

/**
 * work_handler - Work handler to update the leds based on current mod.
 *
 * @work:	Pointer to the work_struct.
 */
static void work_handler(struct work_struct *work)
{
	struct priv *priv = container_of(work, struct priv, work.work);
	uint16_t value;
	unsigned long flags;
	spin_lock_irqsave(&priv->sp, flags);
	// Execute the correct modification to the value
	switch (atomic_read(&priv->mod)) {
	case MOD_NOTHING:
		break;
	case MOD_INC:
		priv->value++;
		break;
	case MOD_DEC:
		priv->value--;
		break;
	case MOD_ROT_LEFT:
		priv->value = (priv->value << 1) |
			      (priv->value >> (NB_LEDS - 1));
		break;
	case MOD_ROT_RIGHT:
		priv->value = (priv->value >> 1) |
			      (priv->value << (NB_LEDS - 1));
		break;
	default:
		break;
	}
	priv->value = priv->value & LEDS_MASK;
	value = priv->value;
	spin_unlock_irqrestore(&priv->sp, flags);

	lc_write(priv, LEDS_OFST, value);

	// Schedule next work iteration
	schedule_delayed_work(&priv->work, msecs_to_jiffies(UPDATE_INTERVAL));
}

static void rearm_pb_interrupts(struct priv *priv)
{
	iowrite8(0x0F, priv->mem_ptr + KEY_IRQ_EDGE_OFST);
}

static irqreturn_t irq_handler(int irq, void *dev_id)
{
	struct priv *priv = (struct priv *)dev_id;
	unsigned long flags;
	uint8_t pressed = ioread8(priv->mem_ptr + KEY_IRQ_EDGE_OFST);

	if (pressed & 0x01) {
		spin_lock_irqsave(&priv->sp, flags);
		priv->value = ioread16(priv->mem_ptr + SWITCH_OFFSET) &
			      SWITCH_MASK;

		spin_unlock_irqrestore(&priv->sp, flags);
		lc_write(priv, LEDS_OFST, priv->value);
	}
	rearm_pb_interrupts(priv);

	return IRQ_HANDLED;
}
/**
 * led_controller_probe - Probe function of the platform driver.
 * @pdev:	Pointer to the platform device structure.
 * Return: 0 on success, negative error code on failure.
 */
static int led_controller_probe(struct platform_device *pdev)
{
	struct priv *priv;
	int rc;
	int btn_interrupt = platform_get_irq(pdev, 0);

	if (btn_interrupt < 0) {
		return btn_interrupt;
	}
	/******* Allocate memory for private data *******/
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (unlikely(!priv)) {
		dev_err(&pdev->dev,
			"Failed to allocate memory for private data\n");
		rc = -ENOMEM;
		goto return_fail;
	}

	// Set the driver data of the platform device to the private data
	platform_set_drvdata(pdev, priv);
	priv->dev = &pdev->dev;
	priv->value = 0;
	atomic_set(&priv->mod, MOD_INC);
	spin_lock_init(&priv->sp);
	/******* Setup memory region pointers *******/
	priv->mem_ptr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->mem_ptr)) {
		dev_err(&pdev->dev, "Failed to remap memory");
		rc = PTR_ERR(priv->mem_ptr);
		goto return_fail;
	}
	/******* Setup Interrupt ******/
	if (devm_request_irq(&pdev->dev, btn_interrupt, irq_handler, 0,
			     "led_controller_btn", priv) < 0) {
		rc = -EBUSY;
		dev_err(priv->dev, "Error while setting up IRQ\n");
		goto return_fail;
	}
	/***** Setup sysfs *****/
	rc = sysfs_create_group(&pdev->dev.kobj, &lc_attr_group);
	if (rc != 0) {
		dev_err(priv->dev, "Error while creating the sysfs group\n");
		goto return_fail;
	}

	/*************** Setup registers ***************/
	// Turn off the leds
	lc_write(priv, LEDS_OFST, 0);
	iowrite8(0xF, priv->mem_ptr + KEY_IRQ_EN_OFST);

	/*************** Setup delayed work ***************/
	INIT_DELAYED_WORK(&priv->work, work_handler);
	schedule_delayed_work(&priv->work, msecs_to_jiffies(UPDATE_INTERVAL));

	dev_info(&pdev->dev, "led_controller probe successful!\n");

	return 0;

return_fail:
	return rc;
}

/**
 * led_controller_remove - Remove function of the platform driver.
 * @pdev:	Pointer to the platform device structure.
 * Return: 0 on success, negative error code on failure.
 */
static int led_controller_remove(struct platform_device *pdev)
{
	// Retrieve the private data from the platform device
	struct priv *priv = platform_get_drvdata(pdev);

	// Turn off the leds
	lc_write(priv, LEDS_OFST, 0);

	sysfs_remove_group(&pdev->dev.kobj, &lc_attr_group);

	// Delete the delayed work
	cancel_delayed_work_sync(&priv->work);

	dev_info(&pdev->dev, "led_controller remove successful!\n");

	return 0;
}

/* Instanciate the list of supported devices */
static const struct of_device_id led_controller_id[] = {
	{ .compatible = "drv2024" },
	{ /* END */ },
};
MODULE_DEVICE_TABLE(of, led_controller_id);

/* Instanciate the platform driver for this driver */
static struct platform_driver led_controller_driver = {
	.driver = {
		.name = "led_controller",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(led_controller_id),
	},
	.probe = led_controller_probe,
	.remove = led_controller_remove,
};

/*
 * As init and exit function only have to register and unregister the
 * platform driver, we can use this helper macros that will automatically
 * create the functions.
 */
module_platform_driver(led_controller_driver);
