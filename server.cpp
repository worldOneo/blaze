#include "httpparser.cpp"
#include "transport.hh"

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

class Server : public blaze::Server {
public:
  void crash(){};
  void boot(){};
  blaze::Action client_connect(blaze::OpenEvent &event) {
    return blaze::Action::NONE;
  };
  void client_close(blaze::CloseEvent &event){};
  blaze::Action traffic(blaze::DataEvent &event) {
    if (blaze::equal(event.data, "quit\r\n")) {
      return blaze::Action::CLOSE;
    }
    blaze::HttpParser parser{};
    blaze::ParseResult result{};
    auto body = parser.parse(event.data, result);
    if (result == blaze::ParseResult::IncompleteData) {
      return blaze::Action::NONE;
    }
    if (result != blaze::ParseResult::Ok) {
      return blaze::Action::CLOSE;
    }
    event.response->write(httpResponse.c_str(), httpResponse.length());
    return blaze::Action::WRITE;
  };
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
  printf("Probing uring\n");
  ring->launch();
  printf("Ready, listen and serve...\n");
  ring->listen_and_serve();
}
