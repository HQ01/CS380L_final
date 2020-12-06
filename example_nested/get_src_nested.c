#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define FILE_SIZE 1 << 20
#define NUM_FILE 1 << 10

int main () {
	char buffer[32];
	char buf[FILE_SIZE];
	for (int j = 0; j < NUM_FILE; j++) {
		snprintf(buffer, sizeof(char) * 32, "./fast_recursive/file%i.bin", j);
		int fd = open(buffer, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
		for (int i = 0; i < FILE_SIZE; i++) {
			buf[i] = 97 + random() % 26;
		}
		write(fd, buf, FILE_SIZE);
		close(fd);
		printf("Creating %d/%d files\n", j, NUM_FILE);
	}
}
