#include <bits/stdc++.h>
#include <errno.h>
#include <fcntl.h>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <unistd.h>

#include "buffer.hh"
#include "pool.hh"
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
  CLOSE,
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
    char *data = ctx->recvBuff.data() + ctx->recvBuff.length();
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

  void socket_close(Context *ctx) {
    CloseEvent event = CloseEvent(ctx->ctxdata);
    server->client_close(event);
    ctx->task = TaskType::CLOSE;
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_close(sqe, ctx->client_fd);
    io_uring_sqe_set_data(sqe, ctx);
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
    if (io_uring_queue_init_params(32768, &ring, &params) < 0) {
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

  void listen_and_serve() {
    Context *ctx = new Context();
    add_accept(ctx);
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
            socket_close(ctx);
          } else {
            ctx->recvBuff.mark_ready(bytes_read);
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
        } else if (type == TaskType::CLOSE) {
          ctx->ctxdata = 0;
          ctx->recvBuff.reset();
          contexes.put(ctx);
        }
      }
      io_uring_cq_advance(&ring, count);
    }
  }
};

class EpollServer : public RingServer {
  Server *server;
  int32_t epoll_fd;
  int32_t server_fd;
  struct sockaddr_in serv_addr {};
  int backlog = 512;
  Pool<Context> contexes{};

  void epoll_add(int fd, uint32_t ep_events) {
    struct epoll_event event;
    auto ctx = contexes.get();
    ctx->client_fd = fd;
    event.events = ep_events;
    event.data.ptr = ctx;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event))
      throw std::runtime_error("add epoll_ctl()");
  }

  static int setnonblocking(int sockfd) {
    if (fcntl(sockfd, F_SETFD, fcntl(sockfd, F_GETFD, 0) | O_NONBLOCK) == -1) {
      return -1;
    }
    return 0;
  }

public:
  EpollServer(Server *s) : server{s} {};

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
  };

  void bind_socket() {
    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      throw std::runtime_error("Failed to bind socket.");
    }

    if (listen(server_fd, backlog) < 0) {
      throw std::runtime_error("Failed to listen to socket.");
    }
  };

  void setup_uring() {
    epoll_fd = epoll_create1(0);
    if (setnonblocking(server_fd) < 0) {
      throw std::runtime_error("Failed to make server socket not blocking");
    }
  };

  void listen_and_serve() {
    size_t event_count = 1024 * 32;
    struct epoll_event events[event_count];
    int epoll_ret;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    epoll_add(server_fd, EPOLLIN);
    while (1) {
      epoll_ret = epoll_wait(epoll_fd, events, event_count, -1);
      if (epoll_ret < 0)
        throw std::runtime_error("epoll_wait()");
      for (int i = 0; i < epoll_ret; i++) {
        Context *ctx = (Context *)events[i].data.ptr;
        if (ctx->client_fd == server_fd) {
          uint32_t client_fd = accept(
              server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
          setnonblocking(client_fd);
          epoll_add(client_fd, EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP);
        } else if (events[i].events & EPOLLIN) {
          ctx->recvBuff.reserve(MAX_MESSAGE_LEN);
          ssize_t bytes_read = read(
              ctx->client_fd, ctx->recvBuff.data() + ctx->recvBuff.length(),
              MAX_MESSAGE_LEN);
          ctx->recvBuff.mark_ready(bytes_read);
          DataEvent event =
              DataEvent(ctx->ctxdata, ctx->recvBuff.view(), &ctx->sendBuff);
          Action result = server->traffic(event);
          if (result == Action::WRITE) {
            ctx->recvBuff.reset();
            write(ctx->client_fd, ctx->sendBuff.data(),
                  ctx->sendBuff.length());
          }
        }
        if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ctx->client_fd, NULL);
          close(ctx->client_fd);
          contexes.put((Context *)events[i].data.ptr);
        }
      }
    }
  };
  ~EpollServer(){};
};

std::unique_ptr<RingServer> create_ring_server(Server *server) {
  return std::make_unique<IORing>(server);
}
} // namespace blaze