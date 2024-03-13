// DRV Labo 1
// Author: André Costa

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LED_REGISTER_BASE 0xFF200000
#define NB_LEDS		  10

void turn_on_led(const uint16_t *led_register, int led)
{
	*(volatile uint16_t *)led_register |= (1 << led);
}
void turn_off_led(const uint16_t *led_register, int led)
{
	*(volatile uint16_t *)led_register &= ~(1 << led);
}

int main()
{
	// Open /dev/mem
	int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
	// Get a pointer to the page where the led register is
	const uint16_t *led_register = mmap(NULL, getpagesize(),
					    PROT_READ | PROT_WRITE, MAP_SHARED,
					    mem_fd, LED_REGISTER_BASE);
	// Close mem_fd now since we no longer need it
	close(mem_fd);
	// Check for output of mmap
	if (led_register == MAP_FAILED) {
		printf("Mapping failed\n");
		return -1;
	}
	// Turn off all leds
	memset((void *)led_register, 0, 2);
	//Loop forever
	for (;;) {
		for (int i = 0; i < NB_LEDS; ++i) {
			turn_on_led(led_register, i);
			sleep(1);
			turn_off_led(led_register, i);
		}
		for (int i = NB_LEDS; i > 0; --i) {
			turn_on_led(led_register, i - 1);
			sleep(1);
			turn_off_led(led_register, i - 1);
		}
	}
	return 0;
}
