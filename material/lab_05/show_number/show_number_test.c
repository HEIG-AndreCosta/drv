#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
	int fd = open("/dev/show_number", O_RDWR);

	if (fd < 0) {
		printf("Error opening device\n");
		return -1;
	}
	uint32_t vals[] = { 1, 2, 3, 4, 0 };
	write(fd, vals, sizeof(vals));
	close(fd);
}