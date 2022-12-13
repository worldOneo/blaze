#include <bits/stdc++.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <strings.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>

#include "buffer.hh"
#include "liburing.h"

#define MAX_CONNECTIONS 4096
#define BACKLOG 512
#define MAX_MESSAGE_LEN 2048
#define BUFFERS_COUNT MAX_CONNECTIONS

void add_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr,
                socklen_t *client_len, unsigned flags);
void add_socket_read(struct io_uring *ring, int fd, unsigned gid, size_t size,
                     unsigned flags);
void add_socket_write(struct io_uring *ring, int fd, __u16 bid, size_t size,
                      unsigned flags);
void add_provide_buf(struct io_uring *ring, __u16 bid, unsigned gid);

enum {
  ACCEPT,
  READ,
  WRITE,
  PROV_BUF,
};

typedef struct conn_info {
  __u32 fd;
  __u16 type;
  __u16 bid;
} conn_info;

class HttpCodec {
  blaze::Buffer<int8_t> buf;
};


char bufs[BUFFERS_COUNT][MAX_MESSAGE_LEN] = {0};
int group_id = 1337;

class IORing {
private:
  struct sockaddr_in serv_addr {};
  struct io_uring_params params {};
  struct io_uring ring {};
  struct sockaddr client_addr {};
  socklen_t client_len = sizeof(client_addr);

  int server_fd{};
  int backlog = 512;

public:
  void setup_on_port(int16_t portno) {
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      throw std::runtime_error("Failed to setup socket.");
    }
    const int val = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &val, sizeof(val)) <
        0) {
      throw std::runtime_error("Failed to configure socket.");
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
  }

  void bind_socket() {
    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      throw std::runtime_error("Failed to bind socket.");
    }

    if (listen(server_fd, backlog) < 0) {
      throw std::runtime_error("Failed to listen to socket.");
    }
  }

  void setup_uring() {
    if (io_uring_queue_init_params(2048, &ring, &params) < 0) {
      throw std::runtime_error("Failed to initialize uring");
    }
    if (!(params.features & IORING_FEAT_FAST_POLL)) {
      throw std::runtime_error("Fast poll is not supported on this kernel.");
    }
    struct io_uring_probe *probe;
    probe = io_uring_get_probe_ring(&ring);
    if (!probe ||
        !io_uring_opcode_supported(probe, IORING_OP_PROVIDE_BUFFERS)) {
      throw std::runtime_error("Failed to select a buffer");
    }
    free(probe);
  }

  void launch() {
    // register buffers for buffer selection
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_provide_buffers(sqe, bufs, MAX_MESSAGE_LEN, BUFFERS_COUNT,
                                  group_id, 0);

    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);
    if (cqe->res < 0) {
      printf("cqe->res = %d\n", cqe->res);
      exit(1);
    }
    io_uring_cqe_seen(&ring, cqe);

    add_accept(&ring, server_fd, &client_addr, &client_len, 0);
  }

  void listen_and_serve() {
    while (1) {
      io_uring_submit_and_wait(&ring, 1);
      struct io_uring_cqe *cqe;
      unsigned head;
      unsigned count = 0;

      // go through all CQEs
      io_uring_for_each_cqe(&ring, head, cqe) {
        ++count;
        struct conn_info conn_i;
        memcpy(&conn_i, &cqe->user_data, sizeof(conn_i));

        int type = conn_i.type;
        if (cqe->res == -ENOBUFS) {
          throw "No buffer received from buffer selection.";
        } else if (type == PROV_BUF) {
          if (cqe->res < 0) {
            printf("cqe->res = %d\n", cqe->res);
            exit(1);
          }
        } else if (type == ACCEPT) {
          int sock_conn_fd = cqe->res;
          // only read when there is no error, >= 0
          if (sock_conn_fd >= 0) {
            add_socket_read(&ring, sock_conn_fd, group_id, MAX_MESSAGE_LEN,
                            IOSQE_BUFFER_SELECT);
          }

          add_accept(&ring, server_fd, (struct sockaddr *)&client_addr,
                     &client_len, 0);
        } else if (type == READ) {
          int bytes_read = cqe->res;
          int bid = cqe->flags >> 16;
          if (cqe->res <= 0) {
            add_provide_buf(&ring, bid, group_id);
            close(conn_i.fd);
          } else {
            add_socket_write(&ring, conn_i.fd, bid, bytes_read, 0);
          }
        } else if (type == WRITE) {
          add_provide_buf(&ring, conn_i.bid, group_id);
          add_socket_read(&ring, conn_i.fd, group_id, MAX_MESSAGE_LEN,
                          IOSQE_BUFFER_SELECT);
        }
      }

      io_uring_cq_advance(&ring, count);
    }
  }
};

int main(int argc, char *argv[]) {
  IORing ring{};
  printf("Preparing fd on port 8080\n");
  ring.setup_on_port(8080);
  printf("Preparing fd for uring\n");
  ring.setup_uring();
  printf("Binding uring\n");
  ring.bind_socket();
  printf("Probing uring\n");
  ring.launch();
  printf("Ready, listen and serve...\n");
  ring.listen_and_serve();
}

void add_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr,
                socklen_t *client_len, unsigned flags) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_accept(sqe, fd, client_addr, client_len, 0);
  io_uring_sqe_set_flags(sqe, flags);

  conn_info conn_i = {
      .fd = fd,
      .type = ACCEPT,
  };
  memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

void add_socket_read(struct io_uring *ring, int fd, unsigned gid,
                     size_t message_size, unsigned flags) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_recv(sqe, fd, NULL, message_size, 0);
  io_uring_sqe_set_flags(sqe, flags);
  sqe->buf_group = gid;

  conn_info conn_i = {
      .fd = fd,
      .type = READ,
  };
  memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

void add_socket_write(struct io_uring *ring, int fd, __u16 bid,
                      size_t message_size, unsigned flags) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_send(sqe, fd, &bufs[bid], message_size, 0);
  io_uring_sqe_set_flags(sqe, flags);

  conn_info conn_i = {
      .fd = fd,
      .type = WRITE,
      .bid = bid,
  };
  memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

void add_provide_buf(struct io_uring *ring, __u16 bid, unsigned gid) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_provide_buffers(sqe, bufs[bid], MAX_MESSAGE_LEN, 1, gid, bid);

  conn_info conn_i = {
      .fd = 0,
      .type = PROV_BUF,
  };
  memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}