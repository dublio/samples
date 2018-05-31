#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libaio.h>

/* zwp10758@gmail.com */

#define BUF_NR_PG 8
#define EVT_COUNT 32
struct iocb **iocbs;
struct io_event io_events[EVT_COUNT];
char *buf[EVT_COUNT];
io_context_t ctx;
size_t page_size;
size_t buf_size;
int fd;



static int alloc_buf(void)
{
	int i;
	struct iocb *tmp;

	iocbs = malloc(sizeof(struct iocb *) * EVT_COUNT);
	if (!iocbs) {
		printf("alloc iocbs failed\n");
		return -1;
	}

	tmp = malloc(sizeof(struct iocb) * EVT_COUNT);
	if (!tmp) {
		printf("alloc iocb failed\n");
		free(iocbs);
		return -1;
	}
	for (i = 0; i < EVT_COUNT; i++)
		iocbs[i] = tmp + i;

	page_size = sysconf(_SC_PAGESIZE);
	buf_size = page_size * BUF_NR_PG;

	for (i = 0; i < EVT_COUNT; i++) {
		buf[i] = aligned_alloc(page_size, buf_size);
		if (!buf[i]) {
			printf("alloc memory %d failed\n", i);
			goto free;
		}
		memset(buf[i], 0, buf_size);
	}

	return 0;

free:
	while (i--)
		free(buf[i]);
	free(iocbs[0]);
	free(iocbs);
	return -1;
}

static void free_buf(void)
{
	int i;

	page_size = sysconf(_SC_PAGESIZE);

	for (i = 0; i < EVT_COUNT; i++) {
		if (buf[i]) {
			free(buf[i]);
			buf[i] = NULL;
		}
	}
	free(iocbs[0]);
	for (i = 0; i < EVT_COUNT; i++)
		iocbs[i] = NULL;
	free(iocbs);
}

void io_callback(io_context_t ctx, struct iocb *iocb, long res, long res2)
{
	printf("iocb:%llu\n", (unsigned long long)iocb);
}

static void setup_single_iocb(struct iocb *iocb, int read, unsigned int index)
{
	memset(iocb, 0, sizeof(*iocb));

	if (read)
		io_prep_pread(iocb, fd, buf[index], buf_size, index * buf_size);
	else
		io_prep_pwrite(iocb, fd, buf[index], buf_size, index * buf_size);
	io_set_callback(iocb, io_callback);

	memset(buf[index], 0, buf_size);
}

static void setup_iocb(void)
{
	int i;

	for (i = 0; i < EVT_COUNT; i++)
		setup_single_iocb(iocbs[i], 0, i);
}

int main(void)
{
	int ret, i;
	int done, inflight;
	io_callback_t cb;
	struct iocb *iocb;

	fd = open("lock.txt", O_CREAT | O_DIRECT | O_RDWR, 0644);
	if (fd == -1) {
		printf("open failed\n");
		return -1;
	}

	ret = io_setup(EVT_COUNT, &ctx);
	if (ret) {
		printf("io setup failed\n");
		goto close;
	}

	ret = alloc_buf();
	if (ret) {
		printf("alloc buffer failed\n");
		goto destroy;
	}

	setup_iocb();

	/* submit all io */
	ret = io_submit(ctx, EVT_COUNT, iocbs);
	if (ret <= 0) {
		printf("io submit failed, %d:%s\n", ret, strerror(ret));
		goto free;
	}
	printf("io submit: %d\n", ret);

	memset(io_events, 0, sizeof(io_events));
	/* wait all io completion */
	ret = io_getevents(ctx, EVT_COUNT, EVT_COUNT, io_events, NULL);
	if (ret <= 0) {
		printf("io_getevent failed, %d:%s\n", ret, strerror(ret));
		goto free;
	}
	printf("io getevents: %d\n", ret);

	for (i = 0; i < EVT_COUNT; i++) {
		cb = (io_callback_t)io_events[i].data;
		iocb = (struct iocb *)io_events[i].obj;
		printf("io_event[%d]:data=%llu, obj=%llu, res=%lld, res2=%lld\n",
			i, io_events[i].data, io_events[i].obj,
			io_events[i].res, io_events[i].res2);
		cb(ctx, iocb, io_events[i].res, io_events[i].res2);
	}

free:
	free_buf();
destroy:
	io_destroy(ctx);
close:
	close(fd);

	return 0;
}

#if 0
           struct iocb {
               __u64   aio_data;
               __u32   PADDED(aio_key, aio_rw_flags);
               __u16   aio_lio_opcode;
               __s16   aio_reqprio;
               __u32   aio_fildes;
               __u64   aio_buf;
               __u64   aio_nbytes;
               __s64   aio_offset;
               __u64   aio_reserved2;
               __u32   aio_flags;
               __u32   aio_resfd;
           };

/* read() from /dev/aio returns these structures. */
struct io_event {
	__u64		data;		/* the data field from the iocb */
	__u64		obj;		/* what iocb this event came from */
	__s64		res;		/* result code for this event */
	__s64		res2;		/* secondary result */
};

#endif
