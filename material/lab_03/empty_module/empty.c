#include <linux/kernel.h> /* Needed for KERN_* */
#include <linux/module.h> /* Needed by all modules */

static int __init empty_init(void)
{
	pr_info("Hello there!\n");
	return 0;
}

static void __exit empty_exit(void)
{
	pr_info("Good bye!\n");
}

MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

module_init(empty_init);
module_exit(empty_exit);
