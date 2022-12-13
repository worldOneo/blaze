#include "../httpparser.cpp"

int main() {
  std::vector<std::pair<std::string, blaze::HttpMethod>> methods{
      {"GET", blaze::HttpMethod::Get},
      {"PUT", blaze::HttpMethod::Put},
      {"POST", blaze::HttpMethod::Post},
      {"OPTIONS", blaze::HttpMethod::Options},
      {"DELETE", blaze::HttpMethod::Delete},
      {"TRACE", blaze::HttpMethod::Trace},
  };
  for (auto [s, m] : methods) {
    blaze::Buffer<char> buffer;
    buffer.write(s.c_str(), s.length());
    if (blaze::requestMethod(buffer.view()) != m) {
      exit(1);
    }
  }
  return 0;
}