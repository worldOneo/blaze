#pragma once
#include <netinet/in.h>
#include <sys/socket.h>

#include "buffer.hh"
#include "liburing.h"

namespace blaze {
class OpenEvent {
public:
  uint64_t context{};
  OpenEvent(uint64_t ctx) : context{ctx} {}
};

class CloseEvent {
public:
  uint64_t context{};
  CloseEvent(uint64_t ctx) : context{ctx} {}
};

class DataEvent {
public:
  uint64_t context{};
  View<char> data;
  Buffer<char> response;
  DataEvent(uint64_t ctx, View<char> data, Buffer<char> response)
      : context{ctx}, data{data}, response{response} {
    this->response.reset();
  }
};

enum Action {
  NONE,
  CLOSE,
};

class Server {
public:
  virtual void crash(){};
  virtual void boot(){};
  virtual Action client_connect(OpenEvent &event) = 0;
  virtual Action client_close(CloseEvent &event) = 0;
  virtual Action traffic(DataEvent &event) = 0;
};

class IORing {
private:
  Server *server;
  struct sockaddr_in serv_addr {};
  struct io_uring_params params {};
  struct io_uring ring {};
  struct sockaddr client_addr {};
  socklen_t client_len = sizeof(client_addr);

  int server_fd{};
  int backlog = 512;

public:
  IORing(Server *server);

  void setup_on_port(int16_t portno);

  void bind_socket();

  void setup_uring();

  void launch();

  void listen_and_serve();
};

} // namespace blaze