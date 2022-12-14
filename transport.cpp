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
  PROV_BUF,
};

class Context {
public:
  uint64_t ctxdata;
  uint64_t bid;
  uint64_t gbid;
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
  Buffer<MessageBuf> buffs{};
  Pool<Context> contexes{};

  int server_fd{};
  int backlog = 512;

  char *buff_of(Context *ctx) { return buffs.ref(ctx->gbid)->data[ctx->bid]; }

  void add_accept(Context *ctx, struct sockaddr *client_addr,
                  socklen_t *client_len, unsigned flags) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, server_fd, client_addr, client_len, 0);
    io_uring_sqe_set_flags(sqe, flags);
    ctx->task = TaskType::ACCEPT;
    sqe->user_data = (int64_t)ctx;
  }

  void socket_read(Context *ctx, size_t message_size, unsigned flags) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_recv(sqe, ctx->client_fd, NULL, message_size, 0);
    io_uring_sqe_set_flags(sqe, flags);
    sqe->buf_group = ctx->gbid;
    ctx->task = TaskType::READ;
    sqe->user_data = (int64_t)ctx;
  }

  void socket_write(Context *ctx, unsigned flags) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_send(sqe, ctx->client_fd, ctx->sendBuff.data(),
                       ctx->sendBuff.length(), 0);
    io_uring_sqe_set_flags(sqe, flags);
    ctx->task = TaskType::WRITE;
    sqe->user_data = (int64_t)ctx;
  }

  void provide_buf(Context *ctx) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_provide_buffers(sqe, buff_of(ctx), MAX_MESSAGE_LEN, 1,
                                  ctx->gbid, ctx->bid);
    ctx->task = TaskType::PROV_BUF;
    sqe->user_data = (int64_t)ctx;
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
    buffs.reserve(1);
    io_uring_prep_provide_buffers(sqe, buffs.ref(0)->data, MAX_MESSAGE_LEN,
                                  BUFFERS_COUNT, 0, 0);
    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);
    if (cqe->res < 0) {
      printf("cqe->res = %d\n", cqe->res);
      exit(1);
    }
    io_uring_cqe_seen(&ring, cqe);
    Context *ctx = new Context();
    add_accept(ctx, &client_addr, &client_len, 0);
  }

  void reset(Context *ctx) {
    ctx->recvBuff.reset();
    provide_buf(ctx);
    close(ctx->client_fd);
    CloseEvent close = CloseEvent(ctx->ctxdata);
    server->client_close(close);
    contexes.put(ctx);
  }

  void handle_action(Action result, Context *ctx) {
    if (result == Action::WRITE) {
      ctx->recvBuff.reset();
      socket_write(ctx, 0);
    } else if (result == Action::CLOSE) {
      reset(ctx);
    }
  }

  void listen_and_serve() {
    Buffer<char> response{};
    Buffer<char> request{};
    Pool<Buffer<char>> responseBuffers;

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

        TaskType type = ctx->task;
        if (cqe->res == -ENOBUFS) {
          throw std::runtime_error("No buffer received from buffer selection.");
        } else if (type == TaskType::PROV_BUF) {
          if (cqe->res < 0) {
            printf("cqe->res = %d\n", cqe->res);
            exit(1);
          }
          contexes.put(ctx);
        } else if (type == TaskType::ACCEPT) {
          int sock_conn_fd = cqe->res;
          if (sock_conn_fd >= 0) {
            Context *other = contexes.get();
            other->client_fd = sock_conn_fd;
            socket_read(other, MAX_MESSAGE_LEN, IOSQE_BUFFER_SELECT);
          }
          add_accept(ctx, (struct sockaddr *)&client_addr, &client_len, 0);
          OpenEvent event = OpenEvent(ctx->ctxdata);
          Action result = server->client_connect(event);
          handle_action(result, ctx);
        } else if (type == TaskType::READ) {
          int bytes_read = cqe->res;
          int bid = cqe->flags >> 16;
          ctx->bid = bid;
          if (cqe->res <= 0) {
            reset(ctx);
          } else {
            ctx->recvBuff.write(buff_of(ctx), bytes_read);
            DataEvent event =
                DataEvent(ctx->ctxdata, ctx->recvBuff.view(), &ctx->sendBuff);
            Action result = server->traffic(event);
            handle_action(result, ctx);
          }
        } else if (type == TaskType::WRITE) {
          provide_buf(ctx);
          Context *other = contexes.get();
          other->client_fd = ctx->client_fd;
          other->bid = ctx->bid;
          socket_read(other, MAX_MESSAGE_LEN, IOSQE_BUFFER_SELECT);
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