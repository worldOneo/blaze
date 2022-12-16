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
    blaze::Buffer<char> data = createBuffer("GET / HTTP/1.0\r\n\r\n");
    parser.parse(data.view(), result);
    assert(result == blaze::ParseResult::Ok);
    assert(blaze::equal(parser.path(), "/"));
    assert(parser.content_length() == -1);
    assert(parser.method() == blaze::HttpMethod::Get);
}