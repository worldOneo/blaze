#include <bits/stdc++.h>
#include <errno.h>
#include <fcntl.h>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <strings.h>
#include <sys/poll.h>
#include <unistd.h>

#include "buffer.hh"
#include "transport.hh"

#define MAX_CONNECTIONS 4096
#define BACKLOG 512
#define MAX_MESSAGE_LEN 2048
#define BUFFERS_COUNT MAX_CONNECTIONS

namespace blaze {
enum TaskType {
  ACCEPT,
  READ,
  WRITE,
  PROV_BUF,
};

struct Context {
public:
  uint64_t ctxdata;
  size_t bid;
  int server_fd;
  int client_fd;
  TaskType task;
};

void add_accept(struct io_uring *ring, Context *ctx,
                struct sockaddr *client_addr, socklen_t *client_len,
                unsigned flags);
void add_socket_read(struct io_uring *ring, Context *ctx, unsigned gid,
                     size_t message_size, unsigned flags);
void add_socket_write(struct io_uring *ring, Context *ctx, size_t size,
                      unsigned flags);
void add_provide_buf(struct io_uring *ring, Context *ctx, unsigned gid);

class HttpCodec {
  blaze::Buffer<int8_t> buf;
};

char bufs[BUFFERS_COUNT][MAX_MESSAGE_LEN] = {0};
int group_id = 1337;

IORing::IORing(Server *server) : server{server} {}

void IORing::setup_on_port(int16_t portno) {
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    throw std::runtime_error("Failed to setup socket.");
  }
  const int val = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &val,
                 sizeof(val)) < 0) {
    throw std::runtime_error("Failed to configure socket.");
  }
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(portno);
  serv_addr.sin_addr.s_addr = INADDR_ANY;
}

void IORing::bind_socket() {
  if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    throw std::runtime_error("Failed to bind socket.");
  }

  if (listen(server_fd, backlog) < 0) {
    throw std::runtime_error("Failed to listen to socket.");
  }
}

void IORing::setup_uring() {
  if (io_uring_queue_init_params(2048, &ring, &params) < 0) {
    throw std::runtime_error("Failed to initialize uring");
  }
  if (!(params.features & IORING_FEAT_FAST_POLL)) {
    throw std::runtime_error("Fast poll is not supported on this kernel.");
  }
  struct io_uring_probe *probe;
  probe = io_uring_get_probe_ring(&ring);
  if (!probe || !io_uring_opcode_supported(probe, IORING_OP_PROVIDE_BUFFERS)) {
    throw std::runtime_error("Failed to select a buffer");
  }
  free(probe);
}

void IORing::launch() {
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
  Context *ctx = new Context();
  ctx->server_fd = server_fd;
  add_accept(&ring, ctx, &client_addr, &client_len, 0);
}

template <typename Data> class Pool {
private:
  std::deque<Data *> pooledItems;

public:
  Data *get() {
    if (pooledItems.empty()) {
      return new Data();
    }
    Data *tmp = pooledItems.front();
    pooledItems.pop_front();
    return tmp;
  }

  void put(Data *item) { pooledItems.push_back(item); }
};

void IORing::listen_and_serve() {
  Buffer<char> response{};
  Buffer<char> request{};
  Pool<Context> contexes;

  server->boot();
  while (1) {
    io_uring_submit_and_wait(&ring, 1);
    struct io_uring_cqe *cqe;
    unsigned head;
    unsigned count = 0;

    // go through all CQEs
    io_uring_for_each_cqe(&ring, head, cqe) {
      ++count;
      Context *ctx = (Context *)cqe->user_data;
      if (ctx == nullptr) {
        throw std::runtime_error("mt context");
      }

      int type = ctx->task;
      if (cqe->res == -ENOBUFS) {
        throw "No buffer received from buffer selection.";
      } else if (type == PROV_BUF) {
        if (cqe->res < 0) {
          printf("cqe->res = %d\n", cqe->res);
          exit(1);
        }
        contexes.put(ctx);
      } else if (type == ACCEPT) {
        int sock_conn_fd = cqe->res;
        if (sock_conn_fd >= 0) {
          Context *other = contexes.get();
          other->server_fd = server_fd;
          other->client_fd = sock_conn_fd;
          add_socket_read(&ring, other, group_id, MAX_MESSAGE_LEN,
                          IOSQE_BUFFER_SELECT);
        }
        add_accept(&ring, ctx, (struct sockaddr *)&client_addr, &client_len, 0);
      } else if (type == READ) {
        int bytes_read = cqe->res;
        int bid = cqe->flags >> 16;
        ctx->bid = bid;
        if (cqe->res <= 0) {
          add_provide_buf(&ring, ctx, group_id);
          close(ctx->client_fd);

          CloseEvent close = CloseEvent(ctx->ctxdata);
          server->client_close(close);
          contexes.put(ctx);
        } else {
          add_socket_write(&ring, ctx, bytes_read, 0);
        }
      } else if (type == WRITE) {
        add_provide_buf(&ring, ctx, group_id);
        Context *other = contexes.get();
        other->server_fd = server_fd;
        other->client_fd = ctx->client_fd;
        other->bid = ctx->bid;
        add_socket_read(&ring, other, group_id, MAX_MESSAGE_LEN,
                        IOSQE_BUFFER_SELECT);
      }
    }

    io_uring_cq_advance(&ring, count);
  }
}

void add_accept(struct io_uring *ring, Context *ctx,
                struct sockaddr *client_addr, socklen_t *client_len,
                unsigned flags) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_accept(sqe, ctx->server_fd, client_addr, client_len, 0);
  io_uring_sqe_set_flags(sqe, flags);
  ctx->task = TaskType::ACCEPT;
  sqe->user_data = (int64_t)ctx;
}

void add_socket_read(struct io_uring *ring, Context *ctx, unsigned gid,
                     size_t message_size, unsigned flags) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_recv(sqe, ctx->client_fd, NULL, message_size, 0);
  io_uring_sqe_set_flags(sqe, flags);
  sqe->buf_group = gid;
  ctx->task = TaskType::READ;
  sqe->user_data = (int64_t)ctx;
}

void add_socket_write(struct io_uring *ring, Context *ctx, size_t message_size,
                      unsigned flags) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_send(sqe, ctx->client_fd, &bufs[ctx->bid], message_size, 0);
  io_uring_sqe_set_flags(sqe, flags);
  ctx->task = TaskType::WRITE;
  sqe->user_data = (int64_t)ctx;
}

void add_provide_buf(struct io_uring *ring, Context *ctx, unsigned gid) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_provide_buffers(sqe, bufs[ctx->bid], MAX_MESSAGE_LEN, 1, gid,
                                ctx->bid);
  ctx->task = TaskType::PROV_BUF;
  sqe->user_data = (int64_t)ctx;
}
} // namespace blaze