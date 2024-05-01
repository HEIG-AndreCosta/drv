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
	{
		uint32_t vals[] = { 123456, 456789, 3, 40, 0 };
		write(fd, vals, sizeof(vals));
	}
	{
		uint32_t vals[] = { 1000000, 999999, 0 };
		write(fd, vals, sizeof(vals));
	}
	close(fd);
}
