#include "transport.hh"

class Server : public blaze::Server {
public:
  void crash(){};
  void boot(){};
  blaze::Action client_connect(blaze::OpenEvent &event) {
    printf("Say hello!!!\n");
    return blaze::Action::NONE;
  };
  void client_close(blaze::CloseEvent &event){
    printf("Cya\n");
  };
  blaze::Action traffic(blaze::DataEvent &event) {
    if (blaze::equal(event.data, "quit\r\n")) {
      return blaze::Action::CLOSE;
    }
    event.response->write_all(event.data);
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
