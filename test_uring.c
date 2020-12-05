#define _GNU_SOURCE
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
    off_t offset;
    int buf_index;
    bool is_read;
    // int *cnt;
};

struct io_uring aio_ring;
int inflight = 0;

/* aio buffer queue */
int aio_buf_queue[QD];
struct iovec aio_buf[QD];
int aio_buf_qhead, aio_buf_qtail;

int aio_buf_queue_init() {
    aio_buf_qhead = 0;
    aio_buf_qtail = QD - 1;
    for (int i = 0; i < QD; i++) 
    {
        // allocate aio buffer
        aio_buf[i].iov_base = NULL;
        aio_buf[i].iov_base = malloc(AIO_BLKSIZE);
        if (aio_buf[i].iov_base == NULL)
        {
            fprintf(stderr, "Error in allocating aio buffer\n");
            return -1;
        }
        aio_buf[i].iov_len = AIO_BLKSIZE;
        memset(aio_buf[i].iov_base, 0, AIO_BLKSIZE);

        // initialize buffer queue
        aio_buf_queue[i] = i;
    }
    return 0;
}

void aio_buf_queue_destroy()
{
    for (int i = 0; i < QD; i++)
        free(aio_buf[i].iov_base);
}

int aio_buf_enqueue(int buf_index) 
{
    int tmp = aio_buf_qtail;
    aio_buf_qtail = (aio_buf_qtail + 1) % QD;
    if (aio_buf_queue[aio_buf_qtail] >= 0) 
    {
        aio_buf_qtail = tmp;
        fprintf(stderr, "Queue is full\n");
        return -1;
    }
    aio_buf_queue[aio_buf_qtail] = buf_index;
    return 0;
}

int aio_buf_dequeue() 
{
    if (aio_buf_queue[aio_buf_qhead] == -1) 
    {
        fprintf(stderr, "Queue is empty\n");
        return -1;
    }
    int buf_index = aio_buf_queue[aio_buf_qhead];
    aio_buf_queue[aio_buf_qhead] = -1;
    aio_buf_qhead = (aio_buf_qhead + 1) % QD;
    return buf_index;
}

/* aio buffer queue */

void aio_prep_rw(struct aio_data *data) 
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&aio_ring);
    if (data->is_read)
        io_uring_prep_readv(sqe, data->src_fd, &aio_buf[data->buf_index], 1, data->offset);
    else 
        io_uring_prep_writev(sqe, data->dst_fd, &aio_buf[data->buf_index], 1, data->offset);
    io_uring_sqe_set_data(sqe, data);
}

void aio_submit()
{
    int ret = io_uring_submit(&aio_ring);
    if (ret < 0)
    {
        fprintf(stderr, "Error in submitting aio requests: %s\n", strerror(-ret));
        exit(1);
    }
}

bool aio_proc_ret(struct io_uring_cqe *cqe)
{
    struct aio_data *data = io_uring_cqe_get_data(cqe);
    if (cqe->res == -EAGAIN || cqe->res == -ECANCELED || (cqe->res >= 0 && cqe->res != aio_buf[data->buf_index].iov_len))
    {
        aio_prep_rw(data);
        aio_submit();
        io_uring_cqe_seen(&aio_ring, cqe);
        return false;
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
        io_uring_cqe_seen(&aio_ring, cqe);
        return false;
    }
    else 
    {
        aio_buf_enqueue(data->buf_index);
        free(data);
        inflight--;
        io_uring_cqe_seen(&aio_ring, cqe);
        return true;
    }
}

int main(int argc, char *argv[]) 
{
    // global
    int ret = io_uring_queue_init(QD, &aio_ring, 0);
    if (ret < 0) 
    {
        fprintf(stderr, "Error in setup io_uring: %s\n", strerror(-ret));
        return 1;
    }
    ret = aio_buf_queue_init();
    if (ret < 0) return 1;
    ret = io_uring_register_buffers(&aio_ring, aio_buf, QD);
    if (ret < 0)
    {
        fprintf(stderr, "Error in registering buffer: %s\n", strerror(-ret));
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

    dst_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dst_fd < 0) {
        close(src_fd);
        perror("open outfile");
        return 1;
    }

    if (fstat(src_fd, &st) < 0)
    {
        close(src_fd);
        close(dst_fd);
        perror("fstat");
        exit(1);
    }
    max_n_read = st.st_size;

    if (fallocate(dst_fd, 0, 0, max_n_read) < 0) {
        close(src_fd);
        close(dst_fd);
        perror("fllocate");
        exit(1);
    }
    // fprintf(stderr, "max_n_read: %ld\n", max_n_read);

    // sparse copy start
    off_t offset = 0;

    while (offset < max_n_read) 
    {
        // fprintf(stderr, "\n offset %ld:", offset);
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
            data->offset = offset;
            // data->iov.iov_base = NULL;
            // data->iov.iov_base = malloc(AIO_BLKSIZE);
            // if (data->iov.iov_base == NULL) 
            // {
            //     fprintf(stderr, "Error in allocating buffer\n");
            //     return 1;
            // }
            data->buf_index = aio_buf_dequeue();
            if (data->buf_index == -1) return 1;
            aio_buf[data->buf_index].iov_len = (max_n_read - offset < AIO_BLKSIZE) ? max_n_read - offset : AIO_BLKSIZE;
            data->is_read = true;

            aio_prep_rw(data);

            offset += aio_buf[data->buf_index].iov_len;
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
                    ret = io_uring_peek_cqe(&aio_ring, &cqe);
                    if (ret == -EAGAIN) break;  // break the loop if no available events
                }
                else // use blocked wait to get at least one event
                {
                    ret = io_uring_wait_cqe(&aio_ring, &cqe);
                }
                
                if (ret < 0) 
                {
                    fprintf(stderr, "Error in io_uring_wait_cqe or io_uring_peek_cqe: %s\n", strerror(-ret));
                    return 1;
                }

                write_comp = aio_proc_ret(cqe);
            }
        }
    }
    fprintf(stderr, "all read submitted\n");

    // wait for all requests to complete
    struct io_uring_cqe *cqe;
    while (inflight > 0)
    {
        ret = io_uring_wait_cqe(&aio_ring, &cqe);
        if (ret < 0) 
        {
            fprintf(stderr, "Error in io_uring_wait_cqe or io_uring_peek_cqe: %s\n", strerror(-ret));
            return 1;
        }
        bool write_comp = aio_proc_ret(cqe);
    }


    close(src_fd);
    close(dst_fd);

    // global
    io_uring_queue_exit(&aio_ring);
    aio_buf_queue_destroy();
    fprintf(stderr, "\ndone\n");

    return 0;
}