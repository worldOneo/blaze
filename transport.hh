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
  Buffer<char>* response;
  DataEvent(uint64_t ctx, View<char> data, Buffer<char>* response)
      : context{ctx}, data{data}, response{response} {
    this->response->reset();
  }
};

enum Action {
  NONE,
  WRITE,
  CLOSE,
};

class Server {
public:
  virtual void crash(){};
  virtual void boot(){};
  virtual void client_close(CloseEvent &event) = 0;
  virtual Action client_connect(OpenEvent &event) = 0;
  virtual Action traffic(DataEvent &event) = 0;
};

class RingServer {
public:
  virtual void setup_on_port(int16_t portno){};

  virtual void bind_socket(){};

  virtual void setup_uring(){};
  
  virtual void listen_and_serve(){};

  virtual ~RingServer(){};
};

std::unique_ptr<RingServer> create_ring_server(Server *server);

} // namespace blaze