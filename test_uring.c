#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <liburing.h>

#define QD  64
#define AIO_BLKSIZE (128 * 1024)

struct aio_data {
    int src_fd;
    int dst_fd;
    off_t size;
    off_t offset;
    struct iovec iov;
    bool is_read;
    // int *cnt;
};

struct io_uring aio_ring;
int inflight = 0;

void aio_prep_rw(struct aio_data *data) 
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(aio_ring);
    if (data->is_read)
        io_uring_prep_readv(sqe, data->src_fd, &data->iov, 1, data->offset);
    else 
        io_uring_prep_writev(sqe, data->dst_fd, &data->iov, 1, data->offset);
    io_uring_sqe_set_data(sqe, data);
}

void aio_submit()
{
    int ret = io_uring_submit(aio_ring);
    if (ret < 0)
    {
        fprintf(stderr, "Error in submitting aio requests: %s\n", strerror(-ret));
        exit(1);
    }
}

void aio_proc_ret(struct io_uring_cqe *cqe)
{
    struct aio_data *data = io_uring_cqe_get_data(cqe);
    if (cqe->res == -EAGAIN || cqe->res == -ECANCELED || (cqe->res >= 0 && cqe->res != data->size))
    {
        aio_prep_rw(data);
        ret = io_uring_submit(aio_ring);
        aio_submit();
        io_uring_cqe_seen(aio_ring, cqe);
    }
    else if (cqe->res < 0) 
    {
        fprintf(stderr, "Error in aio request: %s\n", strerror(-cqe->res));
        exit(1);
    } 
    else if (data->is_read)
    {
        data->is_read = false;
        aio_prep_rw(data);
        aio_submit();
    }
    else 
    {
        free(data->iov.iov_base);
        free(data);
        inflight--;
    }
    io_uring_cqe_seen(aio_ring, cqe);
}

int main(int argc, char *argv[]) 
{
    int ret = o_uring_queue_init(QD, &aio_ring, 0);
    if (ret < 0) 
    {
        fprintf(stderr, "Error in setup io_uring: %s\n", strerror(-ret));
        return 1;
    }

    // argument passed into function
    int src_fd, dst_fd;
    struct stat st;
    off_t max_n_read;

    if (argc < 3) 
    {
        printf("Usage: %s <infile> <outfile>\n", argv[0]);
        return 1;
    }

    src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0) {
        perror("open infile");
        return 1;
    }

    dst_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        perror("open outfile");
        return 1;
    }

    if (fstat(srcfd, &st) < 0)
    {
        perror("fstat");
        exit(1);
    }
    max_n_read = st.st_size;

    // sparse copy start
    off_t offset = 0;
    int ret;

    while (offset < max_n_read) 
    {
        bool need_submit = false; 
        while (offset < max_n_read && inflight < QD) 
        {
            struct aio_data *data = NULL;
            data = (struct aio_data *)malloc(sizeof(struct aio_data));
            if (data == NULL) 
            {
                fprintf(stderr, "Error in allocating aio_data\n");
                return 1;
            }
            data->src_fd = src_fd;
            data->dst_fd = dst_fd;
            data->size = (max_n_read - offset < AIO_BLKSIZE) ? max_n_read - offset : AIO_BLKSIZE;
            data->offset = offset;
            data->iov.iov_base = NULL;
            data->iov.iov_base = malloc(AIO_BLKSIZE);
            if (data->iov.iov_base == NULL) 
            {
                fprintf(stderr, "Error in allocating buffer\n");
                return 1;
            }
            data->iov.iov_len = AIO_BLKSIZE;
            data->is_read = true;

            aio_prep_rw(data);

            offset += data->size;
            inflight++;
            need_submit = true;
        }

        // submit requests
        if (need_submit) 
            aio_submit();

        // process events from finished request
        if (inflight >= QD) 
        {
            struct io_uring_cqe *cqe;
            struct aio_data *data;
            bool write_comp = false;
            while (1)
            {
                if (write_comp) // use unblocked wait to get more available events
                {
                    ret = io_uring_peek_cqe(aio_ring, &cqe);
                    if (ret == -EAGAIN) break;  // break the loop if no available events
                }
                else // use blocked wait to get at least one event
                {
                    ret = io_uring_wait_cqe(aio_ring, &cqe);
                }
                
                if (ret < 0) 
                {
                    fprintf(stderr, "Error in io_uring_wait_cqe or io_uring_peek_cqe: %s\n", strerror(-ret));
                    return 1;
                }

                aio_proc_ret(cqe);
            }
        }
    }

    // wait for all requests to complete
    struct io_uring_cqe *cqe;
    while (inflight > 0)
    {
        ret = io_uring_wait_cqe(aio_ring, &cqe);
        if (ret < 0) 
        {
            fprintf(stderr, "Error in io_uring_wait_cqe or io_uring_peek_cqe: %s\n", strerror(-ret));
            return 1;
        }
        aio_proc_ret(cqe);
    }


    close(src_fd);
    close(dst_fd);

    // global
    io_uring_queue_exit(&aio_ring);

    return 0;
}