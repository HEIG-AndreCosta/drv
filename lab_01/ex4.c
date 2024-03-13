// DRV Labo 1
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

#define LOWER_7_SEG_OFFSET  0x30
#define UPPER_7_SEG_OFFSET  0x20
#define LED_REGISTER_BASE   0xFF200000
#define NB_UPPER_7_SEG	    4
#define NB_LOWER_7_SEG	    2
#define NB_7_SEG	    6
#define NB_LEDS		    10
#define PB_REGISTER_OFESSET 0x50
#define NB_PB		    4
#define KEY_0_MASK	    (1 << 0)
#define KEY_1_MASK	    (1 << 1)
#define KEY_2_MASK	    (1 << 2)
#define KEY_3_MASK	    (1 << 3)
#define FIVE_LEDS_MASK	    0x1F

static const uint8_t ascii_to_7_seg_map[] = {
	0x77, //a
	0x7c, //b
	0x58, //c
	0x5e, //d
	0x79, //e
	0x71, //f
	0x3d, //g
	0x74, //h
	0x30, //i
	0x1e, //j
	0x00, //k (not supported)
	0x38, //l
	0x00, //m (not supported)
	0x54, //n
	0x5c, //o
	0x73, //p
	0x67, //q
	0x50, //r
	0x6d, //s
	0x78, //t
	0x3e, //u
	0x3e, //v (not supported)
	0x00, //w (not supported)
	0x00, //x (not supported)
	0x6e, //y
	0x00, //z (not supported)

};
void write_to_7_seg(const uint8_t *seven_seg_reg, uint8_t value)
{
	*(volatile uint8_t *)seven_seg_reg = value;
}
// Gets the hexadecimal representation of 'c'
uint8_t char_to_7_seg(char c)
{
	int lower_c = tolower(c);
	if (!(lower_c >= 'a' && lower_c <= 'z')) {
		return 0;
	}
	return ascii_to_7_seg_map[lower_c - 'a'];
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

void write_to_led_reg(const uint16_t *led_register, uint16_t value)
{
	*(volatile uint16_t *)led_register = value;
}
// Writes 'c' to the 'index'th register
void write_char_to_7_seg(const uint8_t *lower_seven_seg_reg,
			 const uint8_t *upper_seven_seg_reg, char c, int index)

{
	const uint8_t *write_reg =
		index < NB_LOWER_7_SEG ?
			lower_seven_seg_reg + NB_LOWER_7_SEG - 1 :
			upper_seven_seg_reg + NB_UPPER_7_SEG - 1;
	index = index < NB_LOWER_7_SEG ? index : index - NB_LOWER_7_SEG;
	write_to_7_seg(write_reg - index, char_to_7_seg(c));
}

// Writes 'len' characters from 'word' to the seven segments
void write_word_to_7_seg(const uint8_t *lower_seven_seg_reg,
			 const uint8_t *upper_seven_seg_reg, const char *word,
			 int len)
{
	for (int i = 0; i < len; ++i) {
		write_char_to_7_seg(lower_seven_seg_reg, upper_seven_seg_reg,
				    word[i], i);
	}
}

// Clears the seven segment registers
void clear_7_seg(const uint8_t *lower_seven_seg_reg,
		 const uint8_t *upper_seven_seg_reg)
{
	memset((void *)lower_seven_seg_reg, 0, NB_LOWER_7_SEG);
	memset((void *)upper_seven_seg_reg, 0, NB_UPPER_7_SEG);
}

// Clears the led registers
void clear_leds(const uint16_t *led_register)
{
	memset((void *)led_register, 0, 2);
}

int main()
{
	const char *phrase = "Bienvenue en drv";
	//Open /dev/mem
	int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
	// Get a pointer to the page
	uint8_t *mem_page = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
				 MAP_SHARED, mem_fd, LED_REGISTER_BASE);
	//Close /dev/mem since we no longer need it
	close(mem_fd);

	// Get pointers to each necessary register
	const uint16_t *led_register = (uint16_t *)mem_page;
	const uint8_t *lower_seven_seg_reg = mem_page + LOWER_7_SEG_OFFSET;
	const uint8_t *upper_seven_seg_reg = mem_page + UPPER_7_SEG_OFFSET;
	const uint8_t *pb_register = mem_page + PB_REGISTER_OFESSET;
	// Check mmap error
	if (led_register == MAP_FAILED) {
		printf("Mapping failed\n");
		return -1;
	}

	clear_leds(led_register);
	clear_7_seg(lower_seven_seg_reg, upper_seven_seg_reg);

	write_word_to_7_seg(lower_seven_seg_reg, upper_seven_seg_reg, phrase,
			    NB_7_SEG);

	uint8_t start_index = 0;
	const uint8_t MAX_INDEX = strlen(phrase) - NB_7_SEG;

	for (;;) {
		int btn_pressed = get_btn_pressed(pb_register);

		if (btn_pressed & KEY_0_MASK && start_index > 0) {
			--start_index;
		} else if (btn_pressed & KEY_1_MASK &&
			   start_index < MAX_INDEX) {
			++start_index;
		}

		if (start_index == 0) {
			// Turn on the first 5 leds
			write_to_led_reg(led_register,
					 ~((uint16_t)FIVE_LEDS_MASK));
		} else if (start_index == MAX_INDEX) {
			// Turn on the last 5 leds
			write_to_led_reg(led_register,
					 (uint16_t)FIVE_LEDS_MASK);
		} else {
			// Turn off all leds
			write_to_led_reg(led_register, 0);
		}

		// only update if there was a button press
		if (btn_pressed != 0) {
			clear_7_seg(lower_seven_seg_reg, upper_seven_seg_reg);
			// Write the new phrase
			write_word_to_7_seg(lower_seven_seg_reg,
					    upper_seven_seg_reg,
					    &phrase[start_index], NB_7_SEG);
		}
	}
	return 0;
}
