#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


#define FILENAME "tcp_file.txt"
#define SPLICE_BUF_SIZE	(512 * 1024)
#define RW_BUF_SIZE	(512 * 1024)
#define SOCK_R_SIZE	(2 * 1024 * 1024)
#define SOCK_W_SIZE	(2 * 1024 * 1024)
#define IPADDR "192.1.1.1"
uint16_t port = 5010;
char *ipaddr = IPADDR;
char buf[RW_BUF_SIZE];

char buf_src[RW_BUF_SIZE];


static void buffer_init(void *buf, unsigned int len, uint32_t seed)
{
	int i;

	for (i = 0; i < len; i+= 4)
		*(uint32_t *)(buf + i) = seed + (i >> 2) + ((i >> 2) & 0xff) * 0x01010101;
}

static int buffer_is_equal(void *src, unsigned int src_len, unsigned int src_off, void *dst, unsigned int dst_len)
{
	unsigned int dst_off = 0;
	unsigned int len_now;

	printf("comparing: %d bytes\n", dst_len);

	src_off %= src_len;

	while (dst_len) {

		if ((src_off + dst_len) > src_len)
			len_now = src_len - src_off;
		else
			len_now = dst_len;


		if (!memcmp(src + src_off, dst + dst_off, len_now)) {
			printf("buffers differ\n");
			return 0;
		}

		src_off += len_now;
		if (src_off == src_len)
			src_off = 0;

		dst_off += len_now;
		dst_len -= len_now;
	}

	return 1;
}


static void _receive_splice(int socket_fd, int file_fd)
{
	int pipe_fd[2];
	int len, written, len_prev = 0;
	loff_t offset = 0;

	if (pipe(pipe_fd) < 0) {
		printf("pipe(): %s\n", strerror(errno));
		return;
	}

#if 0
	/* Only works for Linux > 2.6.35 */
	if (fcntl(pipe_fd[0], F_SETPIPE_SZ, RW_BUF_SIZE) < 0)
		printf("fcntl(): %s\n", strerror(errno));

	if (fcntl(pipe_fd[1], F_SETPIPE_SZ, RW_BUF_SIZE) < 0)
		printf("fcntl(): %s\n", strerror(errno));

	if (fcntl(pipe_fd[1], F_GETPIPE_SZ, size) < 0)
		printf("fcntl(): %s\n", strerror(errno));

	printf("pipe size: %d\n", size);
#endif
	while (1) {
		len = splice(socket_fd, NULL, pipe_fd[1], NULL, RW_BUF_SIZE, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
		if (len <= 0) {
			printf("splice in(): %s %d\n", strerror(errno), errno);
			goto done;
		}
#if 0
		if ((len + len_prev) < RW_BUF_SIZE) {
			len_prev += len;
			goto read;
		}

		len += len_prev;
		len_prev = 0;
#endif
		printf("read: %d bytes\n", len);

		while (len) {
			written = splice(pipe_fd[0], NULL, file_fd, &offset, len, SPLICE_F_MOVE | SPLICE_F_MORE);
			if (written < 0) {
				printf("splice out(): %s\n", strerror(errno));
				goto done;
			}

			printf("wrote: %d bytes\n", written);
			len -= written;
		}
	}

done:
	return;
}


static void _loopback_splice(int socket_fd)
{
	int pipe_fd[2];
	int len, written;

	if (pipe(pipe_fd) < 0) {
		printf("pipe(): %s\n", strerror(errno));
		return;
	}

#if 0
	/* Only works for Linux > 2.6.35 */
	if (fcntl(pipe_fd[0], F_SETPIPE_SZ, RW_BUF_SIZE) < 0)
		printf("fcntl(): %s\n", strerror(errno));

	if (fcntl(pipe_fd[1], F_SETPIPE_SZ, RW_BUF_SIZE) < 0)
		printf("fcntl(): %s\n", strerror(errno));

	if (fcntl(pipe_fd[1], F_GETPIPE_SZ, size) < 0)
		printf("fcntl(): %s\n", strerror(errno));

	printf("pipe size: %d\n", size);
#endif
	while (1) {
		len = splice(socket_fd, NULL, pipe_fd[1], NULL, RW_BUF_SIZE, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
		if (len <= 0) {
			printf("splice in(): %s %d\n", strerror(errno), errno);
			goto done;
		}

		printf("read: %d bytes\n", len);

		while (len) {
			written = splice(pipe_fd[0], NULL, socket_fd, NULL, len, SPLICE_F_MOVE | SPLICE_F_MORE);
			if (written < 0) {
				printf("splice out(): %s\n", strerror(errno));
				goto done;
			}

			printf("wrote: %d bytes\n", written);
			len -= written;
		}
	}

done:
	return;
}


static int _write(int fd, void *buf, unsigned int len)
{
	unsigned int offset = 0;
	int written, written_total = 0;

	while (len) {
		written = write(fd, buf + offset, len);
		if (written < 0) {
			printf("write(): %s\n", strerror(errno));

			if (errno == EAGAIN)
				break;

			goto done;
		}

		printf("wrote: %d bytes\n", written);
		written_total += written;
		len -= written;
		offset += written;
	}

	return written_total;

done:
	return -1;
}


static void _receive_to_file(int socket_fd, int file_fd)
{
	int len;

	while (1) {
		len = read(socket_fd, buf, RW_BUF_SIZE);
		if (len <= 0) {
			printf("read(): %s\n", strerror(errno));
			goto done;
		}

		printf("read: %d bytes\n", len);

		if (_write(file_fd, buf, len) < 0)
			goto done;
	}

done:
	return;
}

static void _send_from_file(int socket_fd, int file_fd)
{
	int count;
	int len;

	count = lseek(file_fd, 0, SEEK_END);
	lseek(file_fd, 0, SEEK_SET);

	printf("sending: %d\n", count);

	while (count) {
		len = read(file_fd, buf, RW_BUF_SIZE);
		if (len <= 0) {
			printf("read(): %s\n", strerror(errno));
			goto done;
		}

		printf("read: %d bytes\n", len);

		if (_write(socket_fd, buf, len) < 0)
			goto done;

		count -= len;
	}

done:
	return;
}

static unsigned int receive_to_buffer(int socket_fd, void *buf, unsigned int len)
{
	int rc;
	unsigned int total_read = 0; 

	while (1) {
		rc = read(socket_fd, buf, len);
		if (rc <= 0) {
			printf("read(): %s\n", strerror(errno));
			goto done;
		}

		printf("read: %d bytes\n", rc);
		total_read += rc;
	}

done:
	return total_read;
}


static unsigned int send_from_buffer(int socket_fd, void *buf, unsigned int buf_len, unsigned int buf_off, unsigned int len_total, unsigned int mss)
{
	unsigned int len_now;
	int written;
	unsigned int total_written = 0;

	buf_off %= buf_len;

	printf("sending: %d\n", len_total);

	while (len_total) {
		if ((buf_off + len_total) < buf_len)
			len_now = len_total;
		else
			len_now = buf_len - buf_off;

		if (!mss || (mss > len_now))
			mss = len_now;

		while (len_now) {
			if ((written = _write(socket_fd, buf + buf_off, mss)) <= 0)
				goto done;

			printf("wrote: %d bytes\n", written);

			len_total -= written;
			len_now -= written;
			total_written += written;
			buf_off += written;

			if (buf_off == buf_len)
				buf_off = 0;
		}
	}

done:
	return total_written;
}


static void server(int socket_fd, int file_fd, uint16_t port)
{
	struct sockaddr_in addr;
	int fd;

	printf("server start port:%d\n", port);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("bind(): %s\n", strerror(errno));
		return;
	}

	while (1) {
		if (listen(socket_fd, 0) < 0) {
			printf("listen(): %s\n", strerror(errno));
			return;
		}

		fd = accept(socket_fd,  NULL, NULL);
		if (fd < 0) {
			printf("accept(): %s\n", strerror(errno));
			return;
		}

		printf("server: client start\n");

//		_receive(fd, file_fd);
//		_receive_splice(fd, file_fd);
		_loopback_splice(fd);

		close(fd);

		printf("server: client done\n");
	}

	printf("server done\n");
}

static void send_receive_cmp(int socket_fd)
{
	unsigned int written;
	unsigned int dst_off = 0, src_off = 0;
	unsigned int read;
	unsigned int write_len, total_len;

	write_len = total_len = RW_BUF_SIZE + 500;

	while (total_len) {
		if (write_len) {
			written = send_from_buffer(socket_fd, buf_src, RW_BUF_SIZE, src_off, write_len, 0x100);
			src_off += written;
			write_len -= written;
		}

		read = receive_to_buffer(socket_fd, buf, RW_BUF_SIZE);

		buffer_is_equal(buf_src, RW_BUF_SIZE, dst_off, buf, read);

		dst_off += read;
		total_len -= read;
	}
}

static void client(int socket_fd, int file_fd, uint16_t port)
{
	struct sockaddr_in addr;
	int state = 1;

	printf("client start port %d\n", port);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	inet_pton(AF_INET, ipaddr, &addr.sin_addr.s_addr);

	if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("connect(): %s\n", strerror(errno));
		return;
	}

	if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &state, sizeof(state)) < 0) {
		printf("setsockopt(): %s\n", strerror(errno));
		return;
	}

	if (fcntl(socket_fd, F_SETFL, O_NONBLOCK) < 0) {
		printf("fcntl(): %s\n", strerror(errno));
		return;
	}

	buffer_init(buf_src, RW_BUF_SIZE, 0x01234567);

//	_send_from_file(socket_fd, file_fd);
	send_receive_cmp(socket_fd);

	printf("client done\n");
}

int main(int argc, char *argv[])
{
	int file_fd, socket_fd;
	char *filename = FILENAME;
	int is_client = 0;

	if (argc > 1)
		if (!strncmp(argv[1], "c", 1))
			is_client = 1;

	if (argc > 2) {
		if (is_client)
			ipaddr = argv[2];
		else
			port = strtoul(argv[2], NULL, 0);
	}

	if (argc > 3)
		if (is_client)
			port = strtoul(argv[3], NULL, 0);

	socket_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		printf("socket(): %s\n", strerror(errno));
		goto out;
	}

	if (is_client)
		file_fd = open(filename, O_RDONLY);
	else
		file_fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);

	if (file_fd < 0) {
		printf("open(): %s\n", strerror(errno));
		goto out;
	}

	if (is_client)
		client(socket_fd, file_fd, port);
	else
		server(socket_fd, file_fd, port);

	close(file_fd);
	close(socket_fd);	
out:
	return 0;
}
