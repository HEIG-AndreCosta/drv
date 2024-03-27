#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <poll.h>
#include <signal.h>

#define SEVEN_SEG_OFFSET		  0x20
#define NB_7_SEG			  4
#define NB_LEDS				  10
#define PB_REGISTER_OFFSET		  0x50
#define PB_INTERRUPT_MASK_REGISTER_OFFSET 0x58
#define PB_INTERRUPT_EDGE_REGISTER_OFFSET 0x5C
#define NB_PB				  4
#define KEY_0_MASK			  (1 << 0)
#define KEY_1_MASK			  (1 << 1)
#define KEY_2_MASK			  (1 << 2)
#define KEY_3_MASK			  (1 << 3)
#define FIVE_LEDS_MASK			  0x1F
#define MIN_VALUE			  0
#define MAX_VALUE			  0xF
#define NB_POSSIBILITIES		  4
typedef struct {
	char *response;
	bool is_correct;
} response;

typedef struct {
	char *country;
	response possibilities[NB_POSSIBILITIES];
} question_prompt_t;

question_prompt_t questions[] = {
	{ .country = "Suisse",
	  .possibilities = { { .response = "Zurich", .is_correct = false },
					{ .response = "Geneva", .is_correct = false },
					{ .response = "Bern", .is_correct = true },
					{ .response = "Lausanne", .is_correct = false } } 
	},
	{
		.country = "France",
		.possibilities = { { .response = "Lyon", .is_correct = false },
				   { .response = "Marseille", .is_correct = false },
				   { .response = "Toulouse", .is_correct = false } ,
				   { .response = "Paris", .is_correct = true }}
	},
	{
		.country = "Belgique",
		.possibilities = { { .response = "Anvers", .is_correct = false },
				   { .response = "Gand", .is_correct = false },
				   { .response = "Charleroi", .is_correct = false },
				   { .response = "Bruxelles", .is_correct = true },
        },
	},
	{ .country = "Allemagne",
	  .possibilities = { { .response = "Berlin", .is_correct = true },
				    { .response = "Hambourg", .is_correct = false },
				    { .response = "Munich", .is_correct = false },
			 	    { .response = "Cologne", .is_correct = false } } 
	},
	{ .country = "Espagne",
	  .possibilities = { { .response = "Barcelone", .is_correct = false},
					{ .response = "Madrid", .is_correct = true },
					{ .response = "Valence", .is_correct = false },
					{ .response = "Séville", .is_correct = false } }
	},
};

question_prompt_t *get_random_question(void)
{
	return &questions[rand() % 5];
}

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
	return *(volatile uint8_t *)pb_register;
}
void rearm_pb_interrupts(uint8_t *pb_interrupt_edge_register)
{
	//clear interrupts
	*(volatile uint8_t *)pb_interrupt_edge_register = 0xFF;
}
void enable_pb_interrupts(uint8_t *pb_interrupt_mask_register,
			  uint8_t *pb_interrupt_edge_register)
{
	//enable interrupts
	*(volatile uint8_t *)pb_interrupt_mask_register = 0xFF;
	rearm_pb_interrupts(pb_interrupt_edge_register);
}

void write_to_7_seg(uint32_t *reg, uint8_t value)
{
	*(volatile uint32_t *)reg = int_to_7_seg(value);
}

// Clears the seven segment registers
void clear_7_seg(uint32_t *seven_seg_reg)
{
	memset((void *)seven_seg_reg, 0, NB_7_SEG);
}

// Clears the led registers
void clear_leds(uint16_t *led_register)
{
	memset((void *)led_register, 0, 2);
}

void display_prompt(question_prompt_t *question)
{
	printf("Quelle est la capitale de %s ?\n", question->country);
	for (int i = 0; i < NB_POSSIBILITIES; ++i) {
		printf("\t%d. %s\n", i, question->possibilities[i].response);
	}
}
volatile bool running = true;
void sigint_handler(int signal)
{
	(void)signal;
	running = false;
}
int main()
{
	//initialize random seed
	srand(time(NULL));

	//Open /dev/mem
	int fd = open("/dev/uio0", O_RDWR);
	if (fd < 0) {
		printf("Can't open /dev/uio0\n");
		return -1;
	}

	// Get a pointer to the page
	uint8_t *mem_page = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
				 MAP_SHARED, fd, 0);

	if (mem_page == MAP_FAILED) {
		printf("Mapping failed\n");
		return -1;
	}

	// Get pointers to each necessary register
	uint16_t *const led_register = (uint16_t *)mem_page;
	uint32_t *const seven_seg_reg =
		(uint32_t *)(mem_page + SEVEN_SEG_OFFSET);
	uint8_t *const pb_interrupt_mask_reg =
		mem_page + PB_INTERRUPT_MASK_REGISTER_OFFSET;
	uint8_t *const pb_interrupt_edge_reg =
		mem_page + PB_INTERRUPT_EDGE_REGISTER_OFFSET;

	clear_leds(led_register);
	clear_7_seg(seven_seg_reg);

	write_to_7_seg(seven_seg_reg, MIN_VALUE);

	int8_t score = MIN_VALUE;
	//enable the pb interrupts on the card
	enable_pb_interrupts(pb_interrupt_mask_reg, pb_interrupt_edge_reg);
	question_prompt_t *current_question = NULL;
	struct pollfd fds = {
		.fd = fd,
		.events = POLLIN,
	};
	signal(SIGINT, sigint_handler);

	while (running) {
		if (!current_question) {
			printf("Appuyez sur le bouton 0 pour afficher une question\n");
		}
		uint8_t btn_pressed = 0;

		uint32_t info = 1;
		// Unmask the interrupts
		ssize_t nb_read = write(fd, &info, sizeof(info));

		if (nb_read != (ssize_t)sizeof(info)) {
			printf("Error writing to /dev/uio0\n");
			return -1;
		}

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

		int8_t response = -1;
		if (btn_pressed & KEY_0_MASK) {
			if (!current_question) {
				current_question = get_random_question();
				display_prompt(current_question);
				continue;
			}
			response = 0;
		} else if (btn_pressed & KEY_1_MASK) {
			response = 1;
		} else if (btn_pressed & KEY_2_MASK) {
			response = 2;
		} else if (btn_pressed & KEY_3_MASK) {
			response = 3;
		}

		if (current_question && response != -1) {
			if (current_question->possibilities[response]
				    .is_correct) {
				printf("Bravo, bonne réponse !\n");
				++score;
				if (score > 15) {
					printf("Vous avez gagné!\n");
					break;
				}

			} else {
				score = 0;
				printf("Dommage, mauvaise réponse !\n");
			}
			current_question = NULL;
			write_to_7_seg(seven_seg_reg, score);
		}
	}
	clear_leds(led_register);
	clear_7_seg(seven_seg_reg);
	munmap(mem_page, getpagesize());
	close(fd);
	return 0;
}