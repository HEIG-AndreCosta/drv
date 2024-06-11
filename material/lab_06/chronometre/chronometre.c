#include "linux/delay.h"
#include "linux/irqreturn.h"
#include "linux/jiffies.h"
#include "linux/list.h"
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
#include <linux/math64.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("André Costa");
MODULE_DESCRIPTION("Chronometre");

#define LEDS_OFST	    0x0
#define KEY_OFST	    0x50
#define KEY_IRQ_EN_OFST	    (KEY_OFST + 0x8)
#define KEY_IRQ_EDGE_OFST   (KEY_OFST + 0xC)
#define SWITCH_OFFSET	    (0x40)

#define KEY0_MASK	    (0x01)
#define KEY1_MASK	    (0x02)
#define KEY2_MASK	    (0x04)
#define KEY3_MASK	    (0x08)

#define LED0_MASK	    (0x01)
#define LED1_MASK	    (0x02)
#define LED2_MASK	    (0x04)

#define LED_BLINK_DURATION  msecs_to_jiffies(2000)
#define LED_BLINK_TIME	    msecs_to_jiffies(200)

#define NB_LEDS		    10
#define LEDS_MASK	    ((1 << NB_LEDS) - 1)
#define NB_SWITCH	    10
#define SWITCH_MASK	    ((1 << NB_SWITCH) - 1)
#define UPDATE_INTERVAL_MS  (10) // 10ms
#define DISPLAY_LAP_TIME_MS (3000) // 3s
#define DEV_NAME	    "chronometre"

/**
 * struct priv - Private data for the device
 * @mem_ptr:	Pointer to the IO mapped memory.
 * @dev:	Pointer to the device.
 * @value:	Actual value displayed on the leds.
 * @mod:	Actual mod used to modify the value.
 * @work:	Delayed work used to update the value.
 */
enum chronometre_state { CHRONO_RESET, CHRONO_PAUSE, CHRONO_RUN };
enum chronometre_display_state { CHRONO_DISPLAY_TIME, CHRONO_DISPLAY_LAP };
enum chronometre_lap_display_type {
	CHRONO_LAP_DISPLAY_FROM_START,
	CHRONO_LAP_DISPLAY_SINCE_LAST_LAP,
	CHRONO_LAP_DISPLAY_LEN
};
struct chrono_time {
	uint32_t minutes;
	uint32_t seconds;
	uint32_t millis;
};
struct lap_time {
	struct list_head list;
	uint64_t jiffies;
};
struct chronometre {
	void *mem_ptr;
	struct device *dev;
	struct list_head lap_times;
	size_t lap_times_size;

	struct work_struct work;
	struct work_queue_struct work_queue;
	enum chronometre_state state;
	enum chronometre_display_state display_state;
	enum chronometre_lap_display_type lap_display_type;
	uint64_t start_jiffies;
	uint64_t last_lap_display;
	uint8_t btn_pressed;
	spinlock_t btn_spinlock;
};

#define MS_IN_A_MINUTE (1000 * 60) //1000 second
#define MS_IN_A_SECOND (1000)

static void get_display_time(struct chrono_time *out_display_time,
			     uint64_t start_jiffies, uint64_t end_jiffies)
{
	uint32_t t = jiffies_delta_to_msecs(end_jiffies - start_jiffies);

	out_display_time->minutes = t / MS_IN_A_MINUTE;
	t %= MS_IN_A_MINUTE;
	out_display_time->seconds = t / MS_IN_A_SECOND;
	t %= MS_IN_A_SECOND;
	out_display_time->millis = t;
}
static void get_current_chrono_time(struct chronometre *chrono,
				    struct chrono_time *out_display_time)
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
	struct chrono_time time;
	get_current_chrono_time(priv, &time);

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
	struct chrono_time time;
	get_current_chrono_time(priv, &time);

	return sysfs_emit(
		buf,
		"(0): Display Time Since Start, (1) Display Time Since Last Lap. Current : %d\n",
		priv->lap_display_type ==
			CHRONO_LAP_DISPLAY_SINCE_LAST_LAP); // if true we'll display 1
}

/**
 * lap_display_type_store - Callback to change the lap display time.
 * 0 -> Display time since start
 * 1 -> Display time since last lap
 *
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer to read the data from.
 * @count:	Number of bytes to read.
 * Return: The number of bytes read from the buffer.
 */
static ssize_t lap_display_type_store(struct device *dev,
				      struct device_attribute *attr,
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

static void display_time_in_7_seg(struct chronometre *chrono,
				  struct chrono_time *time)
{
}

static void update_leds(struct chronometre *chrono)
{
	uint8_t mask = 0;

	if (chrono->state == CHRONO_RUN) {
		mask |= LED0_MASK;
	}
	if (chrono->display_state == CHRONO_DISPLAY_LAP) {
		mask |= LED1_MASK;
	}
	if (chrono->lap_times_size > 0) {
		struct lap_time *last_lap;
		uint64_t delta_jiffies;
		uint64_t modulo;
		last_lap = list_last_entry(&chrono->lap_times, struct lap_time,
					   list);

		delta_jiffies = jiffies_64 - last_lap->jiffies;

		/* modulo will either be in range [0;LED_BLINK_TIME] 
		 * or in range [LED_BLINK_TIME; LED_BLINK_TIME *2]
		 * If we're in the lower range, we can turn on the led
		 * else we can turn it off.
		 * This will make the led blink over time
		 */
		div64_u64_rem(delta_jiffies, LED_BLINK_TIME * 2, &modulo);

		if (delta_jiffies < LED_BLINK_DURATION &&
		    modulo < LED_BLINK_TIME) {
			mask |= LED2_MASK;
		}
	}
	lc_write(chrono, LEDS_OFST, mask);
}
/**
 * work_handler - Work handler to update the leds based on current mod.
 *
 * @work:	Pointer to the work_struct.
 */
static void work_handler(struct work_struct *work)
{
	struct chrono_time chrono_time = { .millis = 0,
					   .minutes = 0,
					   .seconds = 0 };
	struct chronometre *chrono =
		container_of(work, struct chronometre, work);

	uint64_t last_lap_display_jiffies = 0;
	struct lap_time *current_lap = NULL;
	struct lap_time *prev_lap = NULL;
	bool is_head;
	uint64_t start_jiffies;

	dev_info(chrono->dev, "Starting chronometre\n");
	while (chrono->state == CHRONO_RUN) {
		switch (chrono->display_state) {
		case CHRONO_DISPLAY_TIME:
			get_current_chrono_time(chrono, &chrono_time);
		case CHRONO_DISPLAY_LAP:
			if (jiffies_64 - last_lap_display_jiffies <
			    msecs_to_jiffies(DISPLAY_LAP_TIME_MS)) {
				//no changes
				continue;
			}
			if (current_lap) {
				current_lap =
					list_next_entry(current_lap, list);

			} else {
				if (!chrono->lap_times_size) {
					chrono->display_state =
						CHRONO_DISPLAY_TIME;
					continue;
				}
				list_first_entry(&chrono->lap_times,
						 struct lap_time, list);
			}

			is_head = list_is_head(&current_lap->list,
					       &chrono->lap_times);
			prev_lap = list_prev_entry(current_lap, list);
			start_jiffies =
				chrono->lap_display_type ==
							CHRONO_LAP_DISPLAY_FROM_START ||
						is_head ?
					chrono->start_jiffies :
					prev_lap->jiffies;

			get_display_time(&chrono_time, start_jiffies,
					 current_lap->jiffies);

			chrono->last_lap_display = jiffies_64;
		}
		update_leds(chrono);
		display_time_in_7_seg(chrono, &chrono_time);
		msleep(UPDATE_INTERVAL_MS);
	}
}

static void rearm_pb_interrupts(struct chronometre *chrono)
{
	iowrite8(0x0F, chrono->mem_ptr + KEY_IRQ_EDGE_OFST);
}

static irqreturn_t thread_irq_handler(int irq, void *dev_id)
{
	struct chronometre *chrono = (struct chronometre *)dev_id;
	struct lap_time *lap;

	dev_info(chrono->dev, "Pressed: %#x\n", chrono->btn_pressed);
	if (chrono->btn_pressed & KEY0_MASK) {
		switch (chrono->state) {
		case CHRONO_RUN:
			dev_info(chrono->dev, "Pausing Chrono\n");
			chrono->state = CHRONO_PAUSE;
			break;
		case CHRONO_PAUSE:
			dev_info(chrono->dev, "Running Chrono\n");
			chrono->state = CHRONO_RUN;
			queue_work(&chrono->work_queue, &chrono->work);
			break;
		case CHRONO_RESET:
			dev_info(chrono->dev, "Enabling Chrono\n");
			chrono->state = CHRONO_RUN;
			chrono->start_jiffies = jiffies_64;
			queue_work(&chrono->work_queue, &chrono->work);
			break;
		}
	}
	if (chrono->btn_pressed & KEY1_MASK) {
		lap = kzalloc(sizeof(*lap), GFP_KERNEL);
		if (lap) {
			lap->jiffies = jiffies_64;
			list_add_tail(&lap->list, &chrono->lap_times);
			chrono->lap_times_size++;
		}
	}
	if (chrono->btn_pressed & KEY2_MASK) {
		switch (chrono->display_state) {
		case CHRONO_DISPLAY_LAP:
			dev_info(chrono->dev, "Display Time\n");
			chrono->display_state = CHRONO_DISPLAY_TIME;
			break;

		case CHRONO_DISPLAY_TIME:
			dev_info(chrono->dev, "Display Lap Time\n");
			chrono->display_state = CHRONO_DISPLAY_LAP;
			break;
		}
	}
	if (chrono->btn_pressed & KEY3_MASK) {
		if (chrono->state == CHRONO_PAUSE) {
			struct chrono_time chrono_time = { .minutes = 0,
							   .seconds = 0,
							   .millis = 0 };
			struct lap_time *lap;
			dev_info(chrono->dev, "Resetting Chrono\n");
			chrono->state = CHRONO_RESET;
			chrono->lap_times_size = 0;
			chrono->start_jiffies = 0;
			list_for_each_entry(lap, &chrono->lap_times, list) {
				list_del(&lap->list);
				kfree(lap);
			}
			display_time_in_7_seg(chrono, &chrono_time);
		}
	}
	return IRQ_HANDLED;
}
static irqreturn_t irq_handler(int irq, void *dev_id)
{
	struct chronometre *chrono = (struct chronometre *)dev_id;
	unsigned long flags;
	spin_lock_irqsave(&chrono->btn_spinlock, flags);
	chrono->btn_pressed = ioread8(chrono->mem_ptr + KEY_IRQ_EDGE_OFST);
	spin_unlock_irqrestore(&chrono->btn_spinlock, flags);
	rearm_pb_interrupts(chrono);

	return IRQ_WAKE_THREAD;
}
/**
 * led_controller_probe - Probe function of the platform driver.
 * @pdev:	Pointer to the platform device structure.
 * Return: 0 on success, negative error code on failure.
 */
static int chronometre_probe(struct platform_device *pdev)
{
	struct chronometre *priv;
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
	priv->lap_times_size = 0;
	priv->state = CHRONO_RESET;
	priv->display_state = CHRONO_DISPLAY_TIME;
	priv->lap_display_type = CHRONO_LAP_DISPLAY_FROM_START;
	priv->btn_pressed = 0;
	INIT_LIST_HEAD(&priv->lap_times);
	/******* Setup memory region pointers *******/
	priv->mem_ptr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->mem_ptr)) {
		dev_err(&pdev->dev, "Failed to remap memory");
		rc = PTR_ERR(priv->mem_ptr);
		goto return_fail;
	}
	/******* Setup Interrupt ******/
	if (devm_request_threaded_irq(&pdev->dev, btn_interrupt, irq_handler,
				      thread_irq_handler, 0,
				      "chronometre_btn_irq", priv) < 0) {
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
	priv->work_queue = create_workqueue("Chrono work queue");
	if (IS_ERR(priv->work_queue)) {
		goto return_fail;
	}
	INIT_WORK(&priv->work, work_handler);

	/*************** Setup registers ***************/
	// Turn off the leds
	lc_write(priv, LEDS_OFST, 0);
	iowrite8(0xF, priv->mem_ptr + KEY_IRQ_EN_OFST);

	/*************** Setup delayed work ***************/

	dev_info(&pdev->dev, "Chrono probe successful!\n");

	return 0;

return_fail:
	return rc;
}

/**
 * led_controller_remove - Remove function of the platform driver.
 * @pdev:	Pointer to the platform device structure.
 * Return: 0 on success, negative error code on failure.
 */
static int chronometre_remove(struct platform_device *pdev)
{
	// Retrieve the private data from the platform device
	struct chronometre *priv = platform_get_drvdata(pdev);

	// Turn off the leds
	lc_write(priv, LEDS_OFST, 0);

	sysfs_remove_group(&pdev->dev.kobj, &lc_attr_group);
	priv->state = CHRONO_RESET;
	// Delete the work
	cancel_work_sync(&priv->work);

	dev_info(&pdev->dev, "led_controller remove successful!\n");

	return 0;
}

/* Instanciate the list of supported devices */
static const struct of_device_id chronometre_id[] = {
	{ .compatible = "drv2024" },
	{ /* END */ },
};
MODULE_DEVICE_TABLE(of, chronometre_id);

/* Instanciate the platform driver for this driver */
static struct platform_driver chonometre_driver = {
	.driver = {
		.name = "led_controller",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(chronometre_id),
	},
	.probe = chronometre_probe,
	.remove = chronometre_remove,
};

/*
 * As init and exit function only have to register and unregister the
 * platform driver, we can use this helper macros that will automatically
 * create the functions.
 */
module_platform_driver(chonometre_driver);
