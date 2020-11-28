#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define SIZE 1 << 20

int main () {
    int fd = open("localdir/src_1G", O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    char buf[SIZE];
    for (int i = 0; i < SIZE; ++i) 
        buf[i] = 'a';
    for (int i = 0; i < 1024; ++i)
        write(fd, buf, SIZE);
    close(fd);
    return 0;
}
