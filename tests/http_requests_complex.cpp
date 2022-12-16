#include "httpparser.hh"
#include <assert.h>

blaze::Buffer<char> createBuffer(std::string data) {
  blaze::Buffer<char> buff{};
  buff.write(data.c_str(), data.length());
  return buff;
}

int main() {
  blaze::HttpParser parser{};
  blaze::ParseResult result{};
  auto t1 = createBuffer("GET /foo/bar HTTP/1.0\r\nHost: cookie.com\r\n\r\n");
  parser.parse(t1.view(), result);
  assert(result == blaze::ParseResult::Ok);
  assert(blaze::equal(parser.path(), "/foo/bar"));
  assert(blaze::equal(parser.find_header("host").value(), "cookie.com"));
  auto t2 = createBuffer("GET / HTTP/1.0\r\nHost: cookie.com\r\nDate: "
                         "foobar\r\nAccept: these/that\r\n\r\n");
  parser.parse(t2.view(), result);
  assert(result == blaze::ParseResult::Ok);
  assert(blaze::equal(parser.find_header("host").value(), "cookie.com"));
  assert(blaze::equal(parser.find_header("date").value(), "foobar"));
  assert(blaze::equal(parser.find_header("accept").value(), "these/that"));

  auto t3 = createBuffer(
      "GET / HTTP/1.0\r\nHost: cookie.com\r\nContent-Length: 50\r\n\r\nData");
  auto data = parser.parse(t3.view(), result);
  assert(result == blaze::ParseResult::Ok);
  assert(blaze::equal(parser.find_header("host").value(), "cookie.com"));
  assert(parser.content_length() == 50);
  assert(blaze::equal(data, "Data"));
}