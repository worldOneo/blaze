#include "httpserver.hh"

std::string httpResponse =
    "HTTP/1.1 200 OK\r\n"
    "Date: Sun, 10 Oct 2010 23:26:07 GMT\r\n"
    "Server: Apache/2.2.8 (Ubuntu) mod_ssl/2.2.8 OpenSSL/0.9.8g\r\n"
    "Last-Modified: Sun, 26 Sep 2010 22:04:35 GMT\r\n"
    "ETag: \"45b6-834-49130cc1182c0\"\r\n"
    "Accept-Ranges: bytes\r\n"
    "Content-Length: 12\r\n"
    "Content-Type: text/html\r\n\r\n"
    "Hello world!";

class Server : public blaze::HttpServer {
public:
  void handle(blaze::HttpRequest &req, blaze::HttpResponse *res) {
    auto buff = res->buffer();
    buff->write("Hello, world!", 13);
    res->write_body(buff->view());
  }
};

int main(int argc, char *argv[]) {
  Server *server = new Server();
  auto ring = blaze::create_ring_server(server);
  printf("Preparing fd on port 8080\n");
  ring->setup_on_port(8080);
  printf("Preparing fd for uring\n");
  ring->setup_uring();
  printf("Binding uring\n");
  ring->bind_socket();
  printf("Ready, listen and serve...\n");
  ring->listen_and_serve();
}
