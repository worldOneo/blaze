#pragma once
#include "buffer.hh"
#include "httpparser.hh"
#include "pool.hh"
#include "transport.hh"

namespace blaze {
class HttpRequest {
private:
  HttpParser _parser;
  View<char> _body;

public:
  HttpRequest(HttpParser p, View<char> body) : _parser{p}, _body{body} {}
  Version version() { return _parser.version(); }
  HttpMethod method() { return _parser.method(); }
  int64_t content_length() { return _parser.content_length(); }
  std::optional<View<char>> host() { return _parser.find_header("Host"); }
  std::optional<View<char>> find_header(std::string header) {
    return _parser.find_header(header);
  }
  View<char> body() { return _body; }
};

class HttpResponse {
private:
  uint16_t _status = 200;
  Buffer<Pair> headers{};
  Buffer<char> body{};
  std::deque<Buffer<char> *> claimed{};

public:
  Pool<Buffer<char>> *buffs;
  void reset();
  void write_body(View<char> data);
  void add_header(View<char> key, View<char> data);
  Buffer<char> *buffer();
  void render(HttpRequest &req, Buffer<char> *buff);
  void status(uint16_t s) { _status = s; }
};

class HttpServer : public Server {
private:
  Pool<Buffer<char>> buffs;
  Pool<HttpResponse> ress;

public:
  void crash();
  void boot();
  Action client_connect(OpenEvent &event);
  void client_close(CloseEvent &event);
  Action traffic(DataEvent &event);
  virtual void handle(HttpRequest &req, HttpResponse *res) = 0;
};
} // namespace blaze