# Laboratoire 2 - DRV

### André Costa

## Quelle est la différence entre ce driver et le Userspace platform driver with generic irq and dynamic memory?

### Help platform driver

│ CONFIG_UIO_PDRV_GENIRQ: │  
 │ │  
 │ Platform driver for Userspace I/O devices, including generic │  
 │ interrupt handling code. Shared interrupts are not supported. │  
 │ │  
 │ This kernel driver requires that the matching userspace driver │  
 │ handles interrupts in a special way. Userspace is responsible │  
 │ for acknowledging the hardware device if needed, and re-enabling │  
 │ interrupts in the interrupt controller using the write() syscall.

### Help dynamic memory

CONFIG_UIO_DMEM_GENIRQ: │  
 │ │  
 │ Platform driver for Userspace I/O devices, including generic │  
 │ interrupt handling code. Shared interrupts are not supported. │  
 │ │  
 │ Memory regions can be specified with the same platform device │  
 │ resources as the UIO_PDRV drivers, but dynamic regions can also │  
 │ be specified. │  
 │ The number and size of these regions is static, │  
 │ but the memory allocation is not performed until │  
 │ the associated device file is opened. The │  
 │ memory is freed once the uio device is closed.

## Réponse

- Avec le driver UIO_PDRV_GENIRQ, on doit mapper les zones memoires qui nous intéressent lors du build time.
- Avec le driver UIO_DMEM_GENIRQ, on peut mapper les zones memoires qui nous intéressent lors du runtime.

## DTS

### Explication des lignes

```dts
#include <dt-bindings/interrupt-controller/irq.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>
```

- Inclure les headers pour les interruptions.

```dts
drv2024 {
    compatible = "drv2024"; # Indique le nom du driver
    reg = <0xFF200000 0x1000>; # Mapper la page où se trouvent les boutons
    interrupts = <GIC_SPI 41 IRQ_TYPE_EDGE_RISING>; # Indique le numéro de l'interruption. SPI = Shared Peripheral Interrupt et pas Serial Peripheral Interface
    interrupt-parent = <&intc>; #Indique sur quel bloc on reçoit l'interrupt
};
```

## Exercice 1

```c
// DRV Labo 2
//Author: André Costa

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

#define SEVEN_SEG_OFFSET    0x20
#define LED_REGISTER_BASE   0xFF200000
#define NB_7_SEG	    4
#define NB_LEDS		    10
#define PB_REGISTER_OFFSET 0x50
#define NB_PB		    4
#define KEY_0_MASK	    (1 << 0)
#define KEY_1_MASK	    (1 << 1)
#define KEY_2_MASK	    (1 << 2)
#define KEY_3_MASK	    (1 << 3)
#define FIVE_LEDS_MASK	    0x1F
#define MIN_VALUE	    0
#define MAX_VALUE	    0xF

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
// Gets the hexadecimal representation of 'c'
uint8_t int_to_7_seg(int i)
{
	if (i >= 0 && i <= 15) {
		return val_to_hex_7_seg[i];
	}
	return 0;
}
/*
 *	Returns the current value of the button register
 *	or 0 if it hasn't changed since last call
 */
int get_btn_pressed(uint8_t *pb_register)
{
	static uint8_t reg_old_value = 0;

	uint8_t reg_new_value = *pb_register;
	if (reg_old_value != reg_new_value) {
		reg_old_value = reg_new_value;
		return *(volatile uint8_t *)pb_register;
	}

	return 0;
}

void write_to_7_seg(uint8_t *reg, uint8_t value)
{
	*(volatile uint8_t *)reg = int_to_7_seg(value);
}

// Clears the seven segment registers
void clear_7_seg(uint8_t *seven_seg_reg)
{
	memset((void *)seven_seg_reg, 0, NB_7_SEG);
}

// Clears the led registers
void clear_leds(uint16_t *led_register)
{
	memset((void *)led_register, 0, 2);
}

int main()
{
	//Open /dev/mem
	int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (mem_fd < 0) {
		printf("Can't open /dev/mem\n");
		return -1;
	}

	// Get a pointer to the page
	uint8_t *mem_page = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
				 MAP_SHARED, mem_fd, LED_REGISTER_BASE);

	//Close /dev/mem since we no longer need it
	close(mem_fd);

	if (mem_page == MAP_FAILED) {
		printf("Mapping failed\n");
		return -1;
	}
	// Get pointers to each necessary register
	uint16_t * led_register = (uint16_t *)mem_page;
	uint8_t * seven_seg_reg = mem_page + SEVEN_SEG_OFFSET;
	uint8_t * pb_register = mem_page + PB_REGISTER_OFFSET;

	clear_leds(led_register);
	clear_7_seg(seven_seg_reg);

	write_to_7_seg(seven_seg_reg, MIN_VALUE);
	int8_t value = MIN_VALUE;

	for (;;) {
		int btn_pressed = get_btn_pressed(pb_register);

		if (btn_pressed & KEY_0_MASK) {
			value = (value + 1) % (MAX_VALUE + 1);
		} else if (btn_pressed & KEY_1_MASK) {
			value = (value - 1);
			if (value < 0) {
				value = MAX_VALUE;
			}
		}
		// only update if there was a button press
		if (btn_pressed != 0) {
			printf("Value: %d\n", value);
			clear_7_seg(seven_seg_reg);
			write_to_7_seg(seven_seg_reg, value);
		}
	}

	clear_leds(led_register);
	clear_7_seg(seven_seg_reg);
	munmap(mem_page, getpagesize());

	return 0;
}
```

### Compilation

```shell
arm-linux-gnueabihf-gcc ex1.c -Wall -Wextra -o ~/export/drv/ex1
```

### Tester

- Lancer dans la DE1-SOC
- Presser sur les touches 0 et 1 pour incrémenter et décrémenter la valeur affichée sur les 7 segments.

## Exercice 2

- 4096 = 4k octets (la taille d'une page en mémoire)
- 5000 n'est pas un multiple de la taille d'une page en mémoire donc on ne peut pas mapper cette espace mémoire.
- 10000 pareil, et en plus on essaierai de mapper trop de mémoire pour nos besoins.

- Avec UIO, on ne pourra pas acquérir n'importe quelle addresse en mémoire. On devra se limiter à ce qui est disponible dans le device tree.

### Avantages Drivers User-Space

- Facilité de développement : Les pilotes en espace utilisateur sont souvent plus faciles à développer car ils n'ont pas besoin d'accéder directement au matériel ou de gérer des tâches de bas niveau, ce qui réduit la complexité du développement.

- Moins de risques pour la stabilité du système : En cas de plantage ou de dysfonctionnement, les pilotes en espace utilisateur ont moins de chance de causer un plantage complet du système car ils s'exécutent dans un espace isolé par rapport au noyau.

### Disavantages Drivers User-Space

- Performance : Les pilotes en espace utilisateur ont tendance à avoir des performances légèrement inférieures aux pilotes en espace noyau en raison des surcoûts de commutation de contexte et de la nécessité de passer par des interfaces utilisateur-noyau.

- Accès restreint au matériel : Les pilotes en espace utilisateur peuvent avoir des limitations d'accès au matériel, ce qui signifie qu'ils peuvent ne pas être en mesure d'exploiter pleinement les fonctionnalités du matériel.

## Exercice 3

-- Ici la seule chose à changer c'est le fichier qu'on ouvre et l'offset pour mmap

```c

	int mem_fd = open("/dev/uio0", O_RDWR);
	if (mem_fd < 0) {
		printf("Can't open /dev/uio0\n");
		return -1;
	}

	// Get a pointer to the page
	uint8_t *mem_page = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
				 MAP_SHARED, mem_fd, 0); // Offset is 0 in case of uio0

```

### Compilation

```shell
arm-linux-gnueabihf-gcc ex3.c -o ~/export/drv/ex3 -Wall -Wextra
```

### Tester

- Lancer dans la DE1-SOC
- Presser sur les touches 0 et 1 pour incrémenter et décrémenter la valeur affichée sur les 7 segments.

## Exercice 4 - 5

Pour les trois versions, on doit à chaque fois unmasker les interruptions sur UIO:

```c
uint32_t info = 1;
// Unmask the interrupts
ssize_t nb_read = write(fd, &info, sizeof(info));
if (nb_read != (ssize_t)sizeof(info)) {
	printf("Error writing to /dev/uio0\n");
	return -1;
}
```

Vu que nous allons bloquer le code jusqu'à ce qu'une interruption se produise, on peut unmasker les interruptions en début de boucle ce qui nous permettra de ne pas rater d'interruptions.

- Pour la première version, on peut juste faire un appel `read()` sur le fichier `/dev/uio0` qui bloquera jusqu'à la prochaine interruption.

```c
// Wait for an interrupt
nb_read = read(fd, &info, sizeof(info));
if (nb_read == (ssize_t)sizeof(info)) {
	//Get the button that was pressed
	btn_pressed = get_btn_pressed(pb_interrupt_edge_reg);

	//Rearm the interrupts
	rearm_pb_interrupts(pb_interrupt_edge_reg);
}
```

- La deuxième version appelle la fonction `poll()` qui va bloquer jusqu'à ce qu'une interruption se produise.

```c
int ret = poll(&fds, 1, -1);
if (ret < 0) {
	printf("Error polling\n");
	break;
} else {
	// Wait for an interrupt
	nb_read = read(fd, &info, sizeof(info));
	if (nb_read == (ssize_t)sizeof(info)) {
		//Get the button that was pressed
		btn_pressed =
			get_btn_pressed(pb_interrupt_edge_reg);

		//Rearm the interrupts
		rearm_pb_interrupts(pb_interrupt_edge_reg);
	}
}
```

- La troisième version utilise la fonction `select()` qui va bloquer jusqu'à ce qu'une interruption se produise.

```c

int ret = select(fd + 1, &fds, NULL, NULL, NULL);

if (ret < 0) {
	printf("Error Selecting\n");
	break;
} else {
	// Wait for an interrupt
	nb_read = read(fd, &info, sizeof(info));
	if (nb_read == (ssize_t)sizeof(info)) {
		//Get the button that was pressed
		btn_pressed =
			get_btn_pressed(pb_interrupt_edge_reg);

		//Rearm the interrupts
		rearm_pb_interrupts(pb_interrupt_edge_reg);
	}
}
```

Il n'y a pas de différence de fonctionnement dans les trois versions. Cependant, les versions avec `poll()` et `select()` permettraient de gérer plusieurs fichiers descripteurs en même temps ainsi que des timeouts si l'on veut pas bloquer indéfiniment.

### Compilation

```shell
arm-linux-gnueabihf-gcc ex4.c -o ~/export/drv/ex4 -Wall -Wextra
arm-linux-gnueabihf-gcc ex4_poll.c -o ~/export/drv/ex4_poll -Wall -Wextra
arm-linux-gnueabihf-gcc ex4_select.c -o ~/export/drv/ex4_select -Wall -Wextra
```

### Tester

- Lancer dans la DE1-SOC
- Repeter en boucle:
  - Presser sur la touche 0 pour afficher la première question
  - Répondre à la question en pressant les touches 0 - 4
