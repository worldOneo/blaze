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
enum class TaskType : int {
  ACCEPT,
  READ,
  WRITE,
};

class Context {
public:
  uint64_t ctxdata;
  int client_fd;
  TaskType task;
  Buffer<char> recvBuff;
  Buffer<char> sendBuff;
};

class HttpCodec {
  blaze::Buffer<int8_t> buf;
};

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

struct MessageBuf {
  char data[MAX_CONNECTIONS][MAX_MESSAGE_LEN];
};

class IORing : public RingServer {
private:
  Server *server;
  struct io_uring ring;
  struct sockaddr_in serv_addr {};
  struct io_uring_params params {};
  struct sockaddr client_addr {};
  socklen_t client_len = sizeof(client_addr);
  Pool<Context> contexes{};

  int server_fd{};
  int backlog = 512;

  void add_accept(Context *ctx) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    ctx->task = TaskType::ACCEPT;
    io_uring_prep_accept(sqe, server_fd, &client_addr, &client_len, 0);
    io_uring_sqe_set_data(sqe, ctx);
  }

  void socket_read(Context *ctx, size_t message_size) {
    ctx->recvBuff.reserve(message_size);
    char *data = ctx->recvBuff.data();
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    ctx->task = TaskType::READ;
    io_uring_prep_recv(sqe, ctx->client_fd, data, message_size, 0);
    io_uring_sqe_set_data(sqe, ctx);
  }

  void socket_write(Context *ctx) {
    char *data = ctx->sendBuff.data();
    size_t size = ctx->sendBuff.length();
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    ctx->task = TaskType::WRITE;
    io_uring_prep_send(sqe, ctx->client_fd, data, size, 0);
    io_uring_sqe_set_data(sqe, ctx);
  }

  void reset(Context *ctx) {
    CloseEvent event = CloseEvent(ctx->ctxdata);
    server->client_close(event);
    ctx->ctxdata = 0;
    ctx->recvBuff.reset();
    close(ctx->client_fd);
    contexes.put(ctx);
  }

public:
  IORing(Server *server) : server{server} {}

  void setup_on_port(int16_t portno) {
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
    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);
    if (cqe->res < 0) {
      printf("cqe->res = %d\n", cqe->res);
      exit(1);
    }
    io_uring_cqe_seen(&ring, cqe);
    Context *ctx = new Context();
    add_accept(ctx);
  }

  void listen_and_serve() {
    Buffer<char> response{};
    Buffer<char> request{};
    Pool<Buffer<char>> responseBuffers;

    server->boot();
    struct io_uring_cqe *cqe;
    while (1) {
      int err = io_uring_submit_and_wait(&ring, 1);
      if (err < 0) {
        std::cerr << "io_uring_wait_cqe error: " << strerror(errno)
                  << std::endl;
        exit(1);
      }
      struct io_uring_cqe *cqe;
      unsigned head;
      unsigned count = 0;
      io_uring_for_each_cqe(&ring, head, cqe) {
        ++count;
        Context *ctx = (Context *)io_uring_cqe_get_data(cqe);
        if (ctx == nullptr) {
          throw std::runtime_error("mt context");
        }

        TaskType type = ctx->task;
        if (type == TaskType::ACCEPT) {
          int sock_conn_fd = cqe->res;
          if (sock_conn_fd >= 0) {
            Context *other = contexes.get();
            other->client_fd = sock_conn_fd;
            socket_read(other, MAX_MESSAGE_LEN);
          }
          add_accept(ctx);
        } else if (type == TaskType::READ) {
          int bytes_read = cqe->res;
          int bid = cqe->flags >> 16;
          int flags = cqe->flags & 0b1111'1111'1111'1111;
          if (flags != 0) {
            throw std::runtime_error("This shouldnt be...");
          } else if (cqe->res <= 0) {
            reset(ctx);
          } else {
            DataEvent event =
                DataEvent(ctx->ctxdata, ctx->recvBuff.view(), &ctx->sendBuff);
            Action result = server->traffic(event);
            if (result == Action::WRITE) {
              ctx->recvBuff.reset();
              socket_write(ctx);
            } else {
              socket_read(ctx, MAX_MESSAGE_LEN);
            }
          }
        } else if (type == TaskType::WRITE) {
          socket_read(ctx, MAX_MESSAGE_LEN);
        }
      }
      io_uring_cq_advance(&ring, count);
    }
  }
};

std::unique_ptr<RingServer> create_ring_server(Server *server) {
  return std::make_unique<IORing>(server);
}
} // namespace blaze