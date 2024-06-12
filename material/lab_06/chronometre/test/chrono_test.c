#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define BUF_SIZE       1024
#define MS_IN_A_MINUTE (1000 * 60) //1000 second
#define MS_IN_A_SECOND (1000)

struct time {
	uint32_t minutes;
	uint32_t secs;
	uint32_t cents;
};
void msecs_to_time(unsigned int msecs, struct time *time)
{
	time->minutes = msecs / MS_IN_A_MINUTE;
	msecs %= MS_IN_A_MINUTE;
	time->secs = msecs / MS_IN_A_SECOND;
	msecs %= MS_IN_A_SECOND;
	time->cents = msecs / 10;
}
int main(void)
{
	int fd = open("/dev/chrono", O_RDWR);

	if (fd < 0) {
		printf("Error opening device\n");
		return -1;
	}
	unsigned int buf[1024];

	ssize_t bytes_read = read(fd, buf, sizeof(buf));
	if (bytes_read <= 0) {
		return 1;
	}
	size_t nbs_read = bytes_read / sizeof(unsigned int);
	struct time t;

	msecs_to_time(buf[0], &t);

	printf("Current Time: %02d:%02d:%02d\n", t.minutes, t.secs, t.cents);
	for (size_t i = 1; i < nbs_read; ++i) {
		msecs_to_time(buf[i], &t);
		printf("Lap %02d: %02d:%02d:%02d\n", i, t.minutes, t.secs,
		       t.cents);
	}
}
