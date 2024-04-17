# Exercice 1

## Un buffer (de byte) alloué dynamiquement est utilisé pour stocker les données.

```c
struct buffer {
	uint8_t *data;
	size_t size;
	size_t capacity;
};

static struct buffer buffer;
```

## Ce buffer démarre avec une capacité de 8 bytes (à l’insertion du module).

```c
#define START_BUFFER_CAPACITY 8
static int __init parrot_init(void)
{
    ...
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
    ...
	kfree(buffer.data);
}
```

## Lors d’une écriture dans /dev/parrot, les données sont copiées dans le buffer depuis la position actuelle dans le fichier. Les anciennes données aux positions écrites sont écrasées, mais le reste du buffer reste intact. Si la capacité du buffer est trop petite pour contenir toutes les données écrites, la capacité est agrandie afin de contenir toutes les nouvelles données jusqu’à une capacité maximale de 1024 bytes. Toute écriture qui accéderait plus loin que ces 1024 bytes sera rejetée avec une erreur.

```c
#define MAX_BUFFER_CAPACITY 1024

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
```

## Lors d’une relecture, les données sont lues depuis la position actuelle dans le fichier et le buffer user-space est, si possible (= assez de donnée dans le buffer), complétement remplis. Seules les données écrites doivent être lues (donc si seulement 5 bytes ont été écrits, seulement ceux-ci sont lus malgré la capacité de départ du buffer de 8 bytes)

```c
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
```

## Compilation

```bash
$ make
Building with kernel sources in /home/andre/dev/heig-vd/drv/linux-socfpga/
make ARCH=arm CROSS_COMPILE=/opt/toolchains/arm-linux-gnueabihf_6.4.1/bin/arm-linux-gnueabihf- -C /home/andre/dev/heig-vd/drv/linux-socfpga/ M=/home/andre/dev/heig-vd/drv/drv24/material/lab_04/parrot_module -W -Wall -Wstrict-prototypes -Wmissing-prototypes
make[1]: Entering directory '/home/andre/dev/heig-vd/drv/linux-socfpga'
  LD [M]  /home/andre/dev/heig-vd/drv/drv24/material/lab_04/parrot_module/parrot.ko
make[1]: Leaving directory '/home/andre/dev/heig-vd/drv/linux-socfpga'
Building userspace test application
/opt/toolchains/arm-linux-gnueabihf_6.4.1/bin/arm-linux-gnueabihf-gcc -o parrot_test parrot_test.c -Wall
$ mv parrot.ko parrot_test ~/export/drv
```

## Test

```bash
root@de1soclinux:~/drv# insmod parrot.ko
root@de1soclinux:~/drv# ./parrot_test
Written data are:
0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0a 0x0b 0x0c 0x0d 0x0e 0x0f
0x10 0x11 0x12 0x13 0x14 0x15 0x16 0x17 0x18 0x19 0x1a 0x1b 0x1c 0x1d 0x1e 0x1f
0x20 0x21 0x22 0x23 0x24 0x25 0x26 0x27 0x28 0x29 0x2a 0x2b 0x2c 0x2d 0x2e 0x2f
0x30 0x31 0x32 0x33 0x34 0x35 0x36 0x37 0x38 0x39 0x3a 0x3b 0x3c 0x3d 0x3e 0x3f
0x40 0x41 0x42 0x43 0x44 0x45 0x46 0x47 0x48 0x49 0x4a 0x4b 0x4c 0x4d 0x4e 0x4f
0x50 0x51 0x52 0x53 0x54 0x55 0x56 0x57 0x58 0x59 0x5a 0x5b 0x5c 0x5d 0x5e 0x5f
0x60 0x61 0x62 0x63 0x64 0x65 0x66 0x67 0x68 0x69 0x6a 0x6b 0x6c 0x6d 0x6e 0x6f
0x70 0x71 0x72 0x73 0x74 0x75 0x76 0x77 0x78 0x79 0x7a 0x7b 0x7c 0x7d 0x7e 0x7f

Read data are:
0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0a 0x0b 0x0c 0x0d 0x0e 0x0f
0x10 0x11 0x12 0x13 0x14 0x15 0x16 0x17 0x18 0x19 0x1a 0x1b 0x1c 0x1d 0x1e 0x1f
0x20 0x21 0x22 0x23 0x24 0x25 0x26 0x27 0x28 0x29 0x2a 0x2b 0x2c 0x2d 0x2e 0x2f
0x30 0x31 0x32 0x33 0x34 0x35 0x36 0x37 0x38 0x39 0x3a 0x3b 0x3c 0x3d 0x3e 0x3f
0x40 0x41 0x42 0x43 0x44 0x45 0x46 0x47 0x48 0x49 0x4a 0x4b 0x4c 0x4d 0x4e 0x4f
0x50 0x51 0x52 0x53 0x54 0x55 0x56 0x57 0x58 0x59 0x5a 0x5b 0x5c 0x5d 0x5e 0x5f
0x60 0x61 0x62 0x63 0x64 0x65 0x66 0x67 0x68 0x69 0x6a 0x6b 0x6c 0x6d 0x6e 0x6f
0x70 0x71 0x72 0x73 0x74 0x75 0x76 0x77 0x78 0x79 0x7a 0x7b 0x7c 0x7d 0x7e 0x7f

All data were correct
```

# Exercice 2
