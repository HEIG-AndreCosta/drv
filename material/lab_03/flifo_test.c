#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include "flifo_module/flifo.h"

int64_t buf[8];
int main()
{
	int fd = open("/dev/flifo", O_RDWR);
	if (fd < 0) {
		printf("Error opening /dev/flifo\n");
		return -1;
	}

	if (ioctl(fd, FLIFO_CMD_CHANGE_MODE, MODE_FIFO) < 0) {
		perror("ioctl:");
		return -2;
	}
	char buffer[8];
	int64_t values[] = { 255, 65536, 2147483647, 2147483649 };

	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		write(fd, &values[i], sizeof(values[i]));
	}

	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		read(fd, buffer, sizeof(values[i]));
		if (*buffer != values[i]) {
			printf("Error: %ld != %ld\n", (int64_t)*buffer,
			       values[i]);
		}
	}
	close(fd);
}