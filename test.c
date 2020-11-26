#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <libaio.h>
#include <time.h>

#define LEN 1024*10240

int main() {
	io_context_t ctx;
	struct iocb iocb;
	struct iocb * iocbs[1];
	struct io_event events[1];
	struct timespec timeout;
	int fd;
	struct timespec start_t, end_t;

	fd = open("./test_file", O_WRONLY | O_CREAT);
	if (fd < 0) err(1, "open");

	memset(&ctx, 0, sizeof(ctx));
	if (io_setup(10, &ctx) != 0) err(1, "io_setup");

	char *msg = NULL;
	posix_memalign((void **)&msg, 512, LEN);
	io_prep_pwrite(&iocb, fd, (void *)msg, LEN, 0);
	iocb.data = (void *)msg;

	iocbs[0] = &iocb;

	clock_gettime(CLOCK_REALTIME, &start_t);
	if (io_submit(ctx, 1, iocbs) != 1) {
		io_destroy(ctx);
		err(1, "io_submit");
	}
	clock_gettime(CLOCK_REALTIME, &end_t);
	double sec = ((double)(end_t.tv_nsec) - start_t.tv_nsec) / 1000000000 + end_t.tv_sec - start_t.tv_sec;
	printf("%.6f\n", sec);

	while (1) {
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;
		if (io_getevents(ctx, 1, 1, events, &timeout) == 1) {
                       printf("%ld\n", events[0].res);
			close(fd);
			break;
		}
		printf("not done yet\n");
		//sleep(1);
	}
	clock_gettime(CLOCK_REALTIME, &end_t);
	sec = ((double)(end_t.tv_nsec) - start_t.tv_nsec) / 1000000000 + end_t.tv_sec - start_t.tv_sec;
	printf("%.6f\n", sec);
	io_destroy(ctx);
	free(msg);

	return 0;
}
