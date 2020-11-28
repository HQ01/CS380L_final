/*
    * Simplistic version of copy command using async i/o
    *
    * From:  Stephen Hemminger <shemminger@osdl.org>
    * Copy file by using a async I/O state machine.
    * 1. Start read request
    * 2. When read completes turn it into a write request
    * 3. When write completes decrement counter and free resources
    *
    *
    * Usage: aiocp file(s) desination
*/
#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>

#include <libaio.h>
#include <pthread.h>

#define AIO_BLKSIZE (128 * 1024)
#define AIO_MAXIO 32
#define AIO_ALIGN_SIZE 512

struct aio_data_t {
    int dst_fd;
    int *req_cnt;
    pthread_mutex_t *req_cnt_mtx;
};

int busy = 0;
pthread_mutex_t busy_mtx;
pthread_cond_t busy_not_empty;
pthread_cond_t busy_not_full;
bool has_done = false;
bool has_error = false;

static const char *dstname = NULL;
static const char *srcname = NULL;
int total_read = 0;
int total_write = 0;

void *rw_done(void *aio_ctx) 
{
    struct timespec timeout = { 0, 1000000 }; // 1 ms timeout

    io_context_t *aio_ctx_ptr = (io_context_t *)aio_ctx;
    struct io_event events[AIO_MAXIO];

    int rw_cnt, rd_cnt;
    while (1)
    {
        int i, j, rc;

        pthread_mutex_lock(&busy_mtx);
        while (busy == 0 && !has_done) // Wait for I/O requests being pushed into queue
            pthread_cond_wait(&busy_not_empty, &busy_mtx);
        if (busy == 0 && has_done) {
            pthread_mutex_unlock(&busy_mtx);
            break;
        }
        pthread_mutex_unlock(&busy_mtx);

        do
        {
            rw_cnt = io_getevents(*aio_ctx_ptr, 1, busy, events, &timeout);
        }
        while (rw_cnt == 0 || rw_cnt == -EINTR);

        if (rw_cnt > 0) 
        {
            rd_cnt = 0;
            for (i = 0; i < rw_cnt; i++)
            {
                struct iocb *aio_req = events[i].obj;
                bool is_read = (aio_req->aio_lio_opcode == IO_CMD_PREAD);

                // aio read/write error handling
                if (events[i].res < 0) 
                {
                    fprintf(stderr, "Error in aio %s: %s\n", is_read ? "read" : "write", strerror(-events[i].res));
                    has_error = true;
                    break;
                }
                else if (events[i].res != aio_req->u.c.nbytes) 
                {
                    fprintf(stderr, "Error in aio %s: missed bytes, expect %ld, got %ld", is_read ? "read" : "write", aio_req->u.c.nbytes, events[i].res);
                    has_error = true;
                    break;
                }

                // Delay processing returned read request but process returned write request
                if (is_read)
                {
                    rd_cnt++;
                    write(2, "r", 1);
                    total_read++;
                }
                else 
                {
                    struct aio_data_t *aio_data = (struct aio_data_t *)(aio_req->data);
                    // bool copy_done = false;

                    // pthread_mutex_lock(aio_data->req_cnt_mtx);
                    // int *req_cnt = aio_data->req_cnt;
                    // (*req_cnt)--;
                    // if (*req_cnt == 0)
                    //     copy_done = true;
                    // pthread_mutex_unlock(aio_data->req_cnt_mtx);

                    // if (copy_done)
                    // {
                    //     close(aio_data->dst_fd);
                    //     free(aio_data->req_cnt);
                    //     pthread_mutex_destroy(aio_data->req_cnt_mtx);
                    // }

                    free(aio_req->u.c.buf);
                    free(aio_req);
                    events[i].obj = NULL;
                    write(2, "w", 1);
                    total_write++;
                }
            }
            
            if (!has_error && rd_cnt > 0) 
            {
                struct iocb *aio_queue[rd_cnt];
                for (i = 0, j = 0; i < rw_cnt; i++) 
                {
                    struct iocb *aio_req = events[i].obj;
                    if (aio_req && aio_req->aio_lio_opcode == IO_CMD_PREAD) {
                        int iosize = aio_req->u.c.nbytes;
                        char *aio_buf = aio_req->u.c.buf;
                        off_t offset = aio_req->u.c.offset;
                        struct aio_data_t *aio_data = (struct aio_data_t *)(aio_req->data);

                        io_prep_pwrite(aio_req, aio_data->dst_fd, aio_buf, iosize, offset);
                        aio_queue[j] = aio_req;
                        j++;
                    }
                }

                rc = io_submit(*aio_ctx_ptr, rd_cnt, aio_queue);
                if (rc < rd_cnt) 
                {
                    fprintf(stderr, "Error in submitting aio requests\n");
                    has_error = true;    
                }
            }
        }
        else 
        {
            fprintf(stderr, "Error in getting aio events: %s\n", strerror(-rw_cnt));
            has_error = true;
        }

        if (has_error)
        {
            pthread_mutex_lock(&busy_mtx);
            pthread_cond_signal(&busy_not_full);
            pthread_mutex_unlock(&busy_mtx);
            break;
        }
        else if (rw_cnt - rd_cnt > 0) 
        {
            pthread_mutex_lock(&busy_mtx);
            if (busy == AIO_MAXIO)  // Wake up main thread if the queue is full perviously
                pthread_cond_signal(&busy_not_full);
            busy -= rw_cnt - rd_cnt;
            pthread_mutex_unlock(&busy_mtx);
        }
    }
}

int main(int argc, char *const *argv)
{
    // global
    pthread_mutex_init(&busy_mtx, NULL);
    pthread_cond_init(&busy_not_empty, NULL);
    pthread_cond_init(&busy_not_full, NULL);

    // argument passed into function
    int srcfd, dstfd;
    struct stat st;
    off_t max_n_read = 0;

    // int req_cnt = 0;
    // pthread_mutex_t req_cnt_mtx;
    // pthread_mutex_init(&req_cnt_mtx, NULL);

    if (argc != 3 || argv[1][0] == '-')
    {
        fprintf(stderr, "Usage: aiocp SOURCE DEST");
        exit(1);
    }
    if ((srcfd = open(srcname = argv[1], O_RDONLY | O_DIRECT)) < 0)
    {
        perror(srcname);
        exit(1);
    }
    if (fstat(srcfd, &st) < 0)
    {
        perror("fstat");
        exit(1);
    }
    max_n_read = st.st_size;

    if ((dstfd = open(dstname = argv[2], O_WRONLY | O_CREAT | O_DIRECT, 0666)) < 0)
    {
        close(srcfd);
        perror(dstname);
        exit(1);
    }
    if (fallocate(dstfd, 0, 0, max_n_read) < 0) {
        close(srcfd);
        close(dstfd);
        perror("fllocate");
        exit(1);
    }

    // sparse copy start
    off_t offset = 0;
    io_context_t aio_ctx;
    pthread_t response;

    /* initialize state machine */
    memset(&aio_ctx, 0, sizeof(aio_ctx));
    io_setup(AIO_MAXIO, &aio_ctx);
    pthread_create(&response, NULL, rw_done, (void *)&aio_ctx);

    // pthread_mutex_lock(&req_cnt_mtx);
    // req_cnt += howmany(max_n_read, AIO_BLKSIZE);
    // pthread_mutex_unlock(&req_cnt_mtx);

    while (offset < max_n_read) 
    {
        int i, rc, n;
        
        pthread_mutex_lock(&busy_mtx);
        while (busy == AIO_MAXIO && !has_error) // Wait for available slot in queue
            pthread_cond_wait(&busy_not_full, &busy_mtx);
        pthread_mutex_unlock(&busy_mtx);

        if (has_error) 
            break;

        // Submit as many reads as once as possible upto AIO_MAXIO 
        n = MIN(AIO_MAXIO - busy, howmany(max_n_read - offset, AIO_BLKSIZE));
        struct iocb *aio_queue[n];
        for (i = 0; i < n; i++)
        {
            struct iocb *aio_req = (struct iocb *)malloc(sizeof(struct iocb));
            char *aio_buf = NULL;
            posix_memalign((void **)&aio_buf, AIO_ALIGN_SIZE, AIO_BLKSIZE);
            struct aio_data_t *aio_data = (struct aio_data_t *)malloc(sizeof(struct aio_data_t));

            if (aio_req == NULL || aio_buf == NULL || aio_data == NULL)
            {
                fprintf(stderr, "Error in allocating iocb, buffer or data\n");
                exit(1);
            }

            int iosize = MIN(max_n_read - offset, AIO_BLKSIZE);
            io_prep_pread(aio_req, srcfd, aio_buf, iosize, offset);
            aio_data->dst_fd = dstfd;
            // aio_data->req_cnt = &req_cnt;
            // aio_data->req_cnt_mtx = &req_cnt_mtx;
            aio_req->data = (void *)aio_data;
            aio_queue[i] = aio_req;
            offset += iosize;
        }

        rc = io_submit(aio_ctx, n, aio_queue);
        if (rc < n) 
        {
            fprintf(stderr, "Error in submitting aio requests\n");
            exit(1);
        }

        pthread_mutex_lock(&busy_mtx);
        if (busy == 0)  // Wake up response thread if the queue is empty previously
            pthread_cond_signal(&busy_not_empty);
        busy += n;
        pthread_mutex_unlock(&busy_mtx);
    }

    pthread_mutex_lock(&busy_mtx);
    has_done = true;
    pthread_cond_signal(&busy_not_empty);
    pthread_mutex_unlock(&busy_mtx);
    pthread_join(response, NULL);

    fprintf(stderr, "\nTotal: read %d blocks, write %d blocks\n", total_read, total_write);
    close(srcfd);
    close(dstfd);
    exit(0);
}