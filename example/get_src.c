#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define SIZE 1 << 20

int main () {
    int fd = open("./src_1G", O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    char buf[SIZE];
    for (int j = 0; j < 1024; j++) {
        for (int i = 0; i < SIZE; i++) 
            buf[i] = 97 + random() % 26;
        write(fd, buf, SIZE);
    }
    close(fd);
    return 0;
}
