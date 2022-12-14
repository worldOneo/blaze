#include "transport.hh"

class Server : public blaze::Server {
public:
  void crash(){};
  void boot(){};
  blaze::Action client_connect(blaze::OpenEvent &event) {
    return blaze::Action::NONE;
  };
  blaze::Action client_close(blaze::CloseEvent &event) {
    return blaze::Action::NONE;
  };
  blaze::Action traffic(blaze::DataEvent &event) { return blaze::Action::NONE; };
};

int main(int argc, char *argv[]) {
  Server *server = new Server();
  blaze::IORing ring(server);
  printf("Preparing fd on port 8080\n");
  ring.setup_on_port(8080);
  printf("Preparing fd for uring\n");
  ring.setup_uring();
  printf("Binding uring\n");
  ring.bind_socket();
  printf("Probing uring\n");
  ring.launch();
  printf("Ready, listen and serve...\n");
  ring.listen_and_serve();
}
