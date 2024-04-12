#ifndef FLIFO_H
#define FLIFO_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#define FLIFO_IOC_MAGIC		    '+'

#define FLIFO_CMD_RESET		    _IO(FLIFO_IOC_MAGIC, 0)
#define FLIFO_CMD_CHANGE_MODE	    _IOW(FLIFO_IOC_MAGIC, 1, int)
#define FLIFO_CMD_CHANGE_VALUE_SIZE _IOW(FLIFO_IOC_MAGIC, 2, int)

#define MODE_FIFO		    0
#define MODE_LIFO		    1

#endif /* FLIFO_H */
