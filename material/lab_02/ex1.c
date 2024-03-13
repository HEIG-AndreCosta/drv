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
#define PB_REGISTER_OFESSET 0x50
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
int get_btn_pressed(const uint8_t *pb_register)
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
void clear_7_seg(const uint8_t *seven_seg_reg)
{
	memset((void *)seven_seg_reg, 0, NB_7_SEG);
}

// Clears the led registers
void clear_leds(const uint16_t *led_register)
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
	uint16_t *const led_register = (uint16_t *)mem_page;
	uint8_t *const seven_seg_reg = mem_page + SEVEN_SEG_OFFSET;
	uint8_t *const pb_register = mem_page + PB_REGISTER_OFESSET;

	clear_leds(led_register);
	clear_7_seg(seven_seg_reg);

	write_to_7_seg(seven_seg_reg, MIN_VALUE);
	uint8_t value = MIN_VALUE;

	for (;;) {
		int btn_pressed = get_btn_pressed(pb_register);

		if (btn_pressed & KEY_0_MASK) {
			value = (value + 1) % (MAX_VALUE + 1);
		} else if (btn_pressed & KEY_1_MASK) {
			value = (value - 1) % (MAX_VALUE + 1);
		}
		// only update if there was a button press
		if (btn_pressed != 0) {
			clear_7_seg(seven_seg_reg);
			// Write the new phrase
			write_to_7_seg(seven_seg_reg, value);
		}
	}

	return 0;
}
