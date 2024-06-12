#include "linux/delay.h"
#include "linux/err.h"
#include "linux/irqreturn.h"
#include "linux/jiffies.h"
#include "linux/leds.h"
#include "linux/list.h"
#include "linux/spinlock.h"
#include "linux/spinlock_types.h"
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
#include <linux/workqueue.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("André Costa");
MODULE_DESCRIPTION("Chronometre");

#define LEDS_OFST	      0x0
#define KEY_OFST	      0x50
#define KEY_IRQ_EN_OFST	      (KEY_OFST + 0x8)
#define KEY_IRQ_EDGE_OFST     (KEY_OFST + 0xC)
#define LOWER_SEVEN_SEG_OFST  (0x20)
#define HIGHER_SEVEN_SEG_OFST (0x30)
#define SWITCH_OFFSET	      (0x40)

#define KEY0_MASK	      (0x01)
#define KEY1_MASK	      (0x02)
#define KEY2_MASK	      (0x04)
#define KEY3_MASK	      (0x08)

#define LED0_MASK	      (0x01)
#define LED1_MASK	      (0x02)
#define LED2_MASK	      (0x04)

#define LED_BLINK_DURATION    msecs_to_jiffies(2000)
#define LED_BLINK_TIME	      msecs_to_jiffies(200)

#define NB_LEDS		      10
#define LEDS_MASK	      ((1 << NB_LEDS) - 1)
#define NB_SWITCH	      10
#define SWITCH_MASK	      ((1 << NB_SWITCH) - 1)
#define UPDATE_INTERVAL_MS    (10) // 10ms
#define DISPLAY_LAP_TIME_MS   (3000) // 3s
#define DEV_NAME	      "chronometre"

static const uint8_t val_to_hex_7_seg[] = {
	0x3F, // 0
	0x06, // 1
	0x5B, // 2
	0x4F, // 3
	0x66, // 4
	0x6D, // 5
	0x7D, // 6
	0x07, // 7
	0x7F, // 8
	0x6F, // 9
	0x77, // a
	0x7C, // b
	0x58, // c
	0x5E, // d
	0x79, // e
	0x71, // f
};

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
	uint32_t cents;
};
struct lap_time {
	struct list_head list;
	uint64_t jiffies_delta_from_start;
};
struct chronometre {
	void *mem_ptr;
	struct device *dev;
	struct list_head lap_times;
	size_t lap_times_size;
	struct lap_time *current_display_lap;

	struct delayed_work list_display_work;
	struct work_struct chrono_work;
	struct workqueue_struct *work_queue;
	enum chronometre_state state;
	enum chronometre_display_state display_state;
	enum chronometre_lap_display_type lap_display_type;
	struct timer_list led2_timer;
	uint64_t led2_trigger_jiffies;
	struct mutex led_mutex;
	spinlock_t btn_spinlock;
	uint64_t start_jiffies;
	uint64_t paused_jiffies;
	uint64_t last_lap_display;
	uint16_t current_led_status;
	uint8_t btn_pressed;
};

#define MS_IN_A_MINUTE (1000 * 60) //1000 second
#define MS_IN_A_SECOND (1000)

/**
 * Gets hexadecimal representation of i for the 7 seg
 */
uint8_t int_to_7_seg(int i)
{
	if (i >= 0 && i <= 15) {
		return val_to_hex_7_seg[i];
	}
	return 0;
}

static void get_display_time(struct chrono_time *out_display_time,
			     uint64_t start_jiffies, uint64_t end_jiffies)
{
	uint32_t t = jiffies_delta_to_msecs(end_jiffies - start_jiffies);

	out_display_time->minutes = t / MS_IN_A_MINUTE;
	t %= MS_IN_A_MINUTE;
	out_display_time->seconds = t / MS_IN_A_SECOND;
	t %= MS_IN_A_SECOND;
	out_display_time->cents = t / 10;
}
static void get_current_chrono_time(struct chronometre *chrono,
				    struct chrono_time *out_display_time)
{
	if (chrono->state != CHRONO_RUN) {
		out_display_time->minutes = 0;
		out_display_time->seconds = 0;
		out_display_time->cents = 0;
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
			  time.cents);
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
	const uint8_t lower_reg[4] = { time->cents % 10, time->cents / 10,
				       time->seconds % 10, time->seconds / 10 };
	const uint8_t higher_reg[2] = { time->minutes % 10,
					time->minutes / 10 };
	const size_t lower_reg_size = sizeof(lower_reg) / sizeof(lower_reg[0]);
	const size_t higher_reg_size =
		sizeof(higher_reg) / sizeof(higher_reg[0]);
	uint32_t lower_reg_val = 0;
	uint32_t higher_reg_val = 0;
	pr_info("%d %d %d %d %d %d %d\n", higher_reg[1], higher_reg[0],
		lower_reg[3], lower_reg[2], lower_reg[1], lower_reg[0],
		time->cents);
	for (size_t i = 0; i < lower_reg_size; ++i) {
		lower_reg_val |= int_to_7_seg(lower_reg[i]) << (i * 8);
	}
	for (size_t i = 0; i < higher_reg_size; ++i) {
		higher_reg_val |= int_to_7_seg(higher_reg[i]) << (i * 8);
	}

	lc_write(chrono, LOWER_SEVEN_SEG_OFST, lower_reg_val);
	lc_write(chrono, HIGHER_SEVEN_SEG_OFST, higher_reg_val);
}
static void on_timer_done(struct timer_list *t)
{
	struct chronometre *chrono = from_timer(chrono, t, led2_timer);
	mutex_lock(&chrono->led_mutex);
	chrono->current_led_status = chrono->current_led_status ^ LED2_MASK;
	iowrite16(chrono->current_led_status, chrono->mem_ptr + LEDS_OFST);
	mutex_unlock(&chrono->led_mutex);

	if (jiffies_64 - chrono->led2_trigger_jiffies < LED_BLINK_DURATION) {
		mod_timer(&chrono->led2_timer,
			  msecs_to_jiffies(LED_BLINK_TIME));
	}
}
static void update_leds(struct chronometre *chrono)
{
	mutex_lock(&chrono->led_mutex);
	if (chrono->state == CHRONO_RUN) {
		chrono->current_led_status |= LED0_MASK;
	} else {
		chrono->current_led_status &= ~LED0_MASK;
	}
	if (chrono->display_state == CHRONO_DISPLAY_LAP) {
		chrono->current_led_status |= LED1_MASK;
	} else {
		chrono->current_led_status &= ~LED1_MASK;
	}

	iowrite16(chrono->current_led_status, chrono->mem_ptr + LEDS_OFST);
	mutex_unlock(&chrono->led_mutex);
}

/**
 * work_handler - Work handler to display the lap time
 *
 * @work:	Pointer to the work_struct.
 */
static void list_display_work_handler(struct work_struct *work)
{
	struct chronometre *chrono =
		container_of(work, struct chronometre, list_display_work.work);
	bool is_head;
	uint64_t start_jiffies;
	struct lap_time *prev_lap;
	struct chrono_time chrono_time = { .cents = 0,
					   .seconds = 0,
					   .minutes = 0 };
	if (chrono->display_state != CHRONO_DISPLAY_LAP) {
		return;
	}
	//Firs iteration, current lap is not assigned
	if (!chrono->current_display_lap) {
		if (!chrono->lap_times_size) {
			chrono->display_state = CHRONO_DISPLAY_TIME;
			pr_info("No Laps to display\n");
			return;
		}
		chrono->current_display_lap = list_first_entry(
			&chrono->lap_times, struct lap_time, list);
	} else {
		chrono->current_display_lap =
			list_next_entry(chrono->current_display_lap, list);

		if (list_is_head(&chrono->current_display_lap->list,
				 &chrono->lap_times)) {
			//wrap around
			//finished displaying the list
			chrono->current_display_lap = NULL;
			chrono->display_state = CHRONO_DISPLAY_TIME;
			return;
		}
	}

	is_head = list_is_head(&chrono->current_display_lap->list,
			       &chrono->lap_times);
	if (is_head ||
	    chrono->lap_display_type == CHRONO_LAP_DISPLAY_FROM_START) {
		start_jiffies = 0;
	} else {
		prev_lap = list_prev_entry(chrono->current_display_lap, list);
		start_jiffies = prev_lap->jiffies_delta_from_start;
	}

	get_display_time(&chrono_time, start_jiffies,
			 chrono->current_display_lap->jiffies_delta_from_start);
	display_time_in_7_seg(chrono, &chrono_time);
	schedule_delayed_work(&chrono->list_display_work,
			      msecs_to_jiffies(DISPLAY_LAP_TIME_MS));
}
/**
 * work_handler - Work handler to update the chronometer time
 *
 * @work:	Pointer to the work_struct.
 */
static void work_handler(struct work_struct *work)
{
	struct chrono_time chrono_time = { .cents = 0,
					   .minutes = 0,
					   .seconds = 0 };
	struct chronometre *chrono =
		container_of(work, struct chronometre, chrono_work);

	dev_info(chrono->dev, "Starting chronometre\n");
	while (chrono->state == CHRONO_RUN) {
		if (chrono->display_state == CHRONO_DISPLAY_TIME) {
			get_current_chrono_time(chrono, &chrono_time);
			display_time_in_7_seg(chrono, &chrono_time);
		}
		update_leds(chrono);
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
	uint64_t delta_paused_jiffies =
		chrono->paused_jiffies - chrono->start_jiffies;

	dev_info(chrono->dev, "Pressed: %#x\n", chrono->btn_pressed);
	if (chrono->btn_pressed & KEY0_MASK) {
		switch (chrono->state) {
		case CHRONO_RUN:
			dev_info(chrono->dev, "Pausing Chrono\n");
			chrono->state = CHRONO_PAUSE;
			chrono->paused_jiffies = jiffies_64;
			break;
		case CHRONO_PAUSE:
			dev_info(chrono->dev, "Running Chrono\n");
			chrono->state = CHRONO_RUN;
			chrono->start_jiffies =
				jiffies_64 -
				delta_paused_jiffies; // update start time
			queue_work(chrono->work_queue, &chrono->chrono_work);
			break;
		case CHRONO_RESET:
			dev_info(chrono->dev, "Enabling Chrono\n");
			chrono->state = CHRONO_RUN;
			chrono->start_jiffies = jiffies_64;
			queue_work(chrono->work_queue, &chrono->chrono_work);
			break;
		}
	}
	if (chrono->btn_pressed & KEY1_MASK) {
		lap = kzalloc(sizeof(*lap), GFP_KERNEL);
		if (lap) {
			lap->jiffies_delta_from_start =
				jiffies_64 - chrono->start_jiffies;
			list_add_tail(&lap->list, &chrono->lap_times);
			chrono->lap_times_size++;
			mod_timer(&chrono->led2_timer, 0);
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
			schedule_delayed_work(&chrono->list_display_work, 0);
			break;
		}
	}
	if (chrono->btn_pressed & KEY3_MASK) {
		if (chrono->state == CHRONO_PAUSE) {
			struct chrono_time chrono_time = { .minutes = 0,
							   .seconds = 0,
							   .cents = 0 };
			struct lap_time *lap;
			struct lap_time *tmp;
			dev_info(chrono->dev, "Resetting Chrono\n");
			chrono->state = CHRONO_RESET;
			chrono->lap_times_size = 0;
			chrono->start_jiffies = 0;
			list_for_each_entry_safe(lap, tmp, &chrono->lap_times,
						 list) {
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
		rc = PTR_ERR(priv->work_queue);
		goto return_fail;
	}
	INIT_WORK(&priv->chrono_work, work_handler);

	timer_setup(&priv->led2_timer, on_timer_done, 0);
	mutex_init(&priv->led_mutex);
	/*************** Setup registers ***************/
	// Turn off the leds
	lc_write(priv, LEDS_OFST, 0);
	iowrite8(0xF, priv->mem_ptr + KEY_IRQ_EN_OFST);

	/*************** Setup delayed work ***************/
	INIT_DELAYED_WORK(&priv->list_display_work, list_display_work_handler);
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
	cancel_work_sync(&priv->chrono_work);

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
