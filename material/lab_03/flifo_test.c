#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include "flifo_module/flifo.h"

/**
 * @brief Set the mode of the list
 * 
 * @param fd 
 * @param mode 
 * @return int 
 */
int set_mode(int fd, int mode)
{
	if (ioctl(fd, FLIFO_CMD_CHANGE_MODE, mode) < 0) {
		return -1;
	}
	return 0;
}

/**
 * @brief reset the list
 * 
 * @param fd 
 * @return int 
 */
int reset(int fd)
{
	if (ioctl(fd, FLIFO_CMD_RESET) < 0) {
		return -1;
	}
	return 0;
}
/**
 * @brief Set the value size
 * 
 * @param fd 
 * @param size 
 * @return int 
 */
int set_value_size(int fd, size_t size)
{
	if (ioctl(fd, FLIFO_CMD_CHANGE_VALUE_SIZE, size) < 0) {
		return -1;
	}
	return 0;
}

/**
 * @brief  Read a value from the file descriptor and check if it is equal to the expected value
 * 
 * @param fd 
 * @param expected_value 
 * @param value_size 
 * @return int 
 */
int read_and_check_value(int fd, uint64_t expected_value, size_t value_size)
{
	uint64_t value = 0;
	ssize_t nb_read = read(fd, (void *)&value, value_size);
	if (nb_read < 0) {
		perror("read:");
		return -1;
	}

	if (value != expected_value) {
		printf("Error: %ld != %ld\n", value, expected_value);
		return -1;
	}

	return 0;
}

// Create a test function for a given type
#define CREATE_TEST_FUNCTION(type)                                     \
	int test_##type(int fd)                                        \
	{                                                              \
		type values[] = { 1, 2, 3, 4 };                        \
		size_t nb_values = sizeof(values) / sizeof(values[0]); \
                                                                       \
		if (set_value_size(fd, sizeof(type)) < 0) {            \
			perror("set_value_size:");                     \
			return -1;                                     \
		}                                                      \
		write(fd, values, nb_values * sizeof(type));           \
                                                                       \
		for (size_t i = 0; i < nb_values; i++) {               \
			if (read_and_check_value(fd, values[i],        \
						 sizeof(type)) < 0) {  \
				return -1;                             \
			}                                              \
		}                                                      \
		if (set_mode(fd, MODE_LIFO) < 0) {                     \
			perror("set_mode:");                           \
			return -1;                                     \
		}                                                      \
		write(fd, values, nb_values * sizeof(type));           \
                                                                       \
		for (size_t i = nb_values; i > 0; i--) {               \
			if (read_and_check_value(fd, values[i - 1],    \
						 sizeof(type)) < 0) {  \
				return -1;                             \
			}                                              \
		}                                                      \
                                                                       \
		return 0;                                              \
	}

CREATE_TEST_FUNCTION(uint8_t);
CREATE_TEST_FUNCTION(uint16_t);
CREATE_TEST_FUNCTION(uint32_t);
CREATE_TEST_FUNCTION(uint64_t);

// make sure we can't read more than what we put in
int test_overflow(int fd)
{
	uint64_t values[] = { 1, 2, 3, 4 };
	size_t nb_values = sizeof(values) / sizeof(values[0]);

	if (reset(fd) < 0) {
		perror("reset:");
		return -1;
	}
	if (set_mode(fd, MODE_FIFO) < 0) {
		perror("set_mode:");
		return -1;
	}

	if (set_value_size(fd, sizeof(uint64_t)) < 0) {
		perror("set_value_size:");
		return -1;
	}
	write(fd, values, sizeof(values));

	for (size_t i = 0; i < nb_values; i++) {
		if (read_and_check_value(fd, values[i], sizeof(uint64_t)) < 0) {
			return -1;
		}
	}

	uint64_t value = 0;
	ssize_t nb_read = read(fd, (void *)&value, sizeof(uint64_t));
	if (nb_read > 0) {
		// We should not be able to read more than the number of values
		return -1;
	}

	return 0;
}

//test the read of multiple values at once
int test_multi_read(int fd)
{
	uint64_t values[] = { 1, 2, 3, 4 };
	size_t nb_values = sizeof(values) / sizeof(values[0]);

	if (reset(fd) < 0) {
		perror("reset:");
		return -1;
	}
	if (set_mode(fd, MODE_FIFO) < 0) {
		perror("set_mode:");
		return -1;
	}

	if (set_value_size(fd, sizeof(uint64_t)) < 0) {
		perror("set_value_size:");
		return -1;
	}
	write(fd, values, sizeof(values));
	uint64_t target_buffer[nb_values];

	ssize_t nb_read = read(fd, target_buffer, sizeof(target_buffer));
	if (nb_read != (ssize_t)sizeof(target_buffer)) {
		return -1;
	}
	for (size_t i = 0; i < nb_values; i++) {
		if (target_buffer[i] != values[i]) {
			return -1;
		}
	}

	if (set_mode(fd, MODE_LIFO) < 0) {
		perror("set_mode:");
		return -1;
	}
	write(fd, values, sizeof(values));
	nb_read = read(fd, target_buffer, sizeof(target_buffer));
	if (nb_read != (ssize_t)sizeof(target_buffer)) {
		return -1;
	}
	for (size_t i = 0; i < nb_values; i++) {
		if (target_buffer[i] != values[nb_values - i - 1]) {
			return -1;
		}
	}
	return 0;
}
int (*test_functions[])(int) = { test_uint8_t,	test_uint16_t, test_uint32_t,
				 test_uint64_t, test_overflow, test_multi_read };
char *test_names[] = { "uint8_t",  "uint16_t", "uint32_t",
		       "uint64_t", "overflow", "multi-read" };
int main()
{
	int fd = open("/dev/flifo", O_RDWR);
	if (fd < 0) {
		printf("Error opening /dev/flifo\n");
		return -1;
	}

	size_t function_count =
		sizeof(test_functions) / sizeof(test_functions[0]);

	for (size_t i = 0; i < function_count; i++) {
		if (reset(fd) < 0) {
			perror("ioctl:");
			goto end;
		}
		if (set_mode(fd, MODE_FIFO) < 0) {
			perror("set_mode:");
			goto end;
		}
		printf("Testing %s: ", test_names[i]);
		if (test_functions[i](fd) < 0) {
			printf("KO\n");
		} else {
			printf("OK\n");
		}
	}
end:
	close(fd);
}