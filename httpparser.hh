#pragma once
#include "buffer.hh"
#include <optional>

namespace blaze {

enum ParseResult {
  IncompleteData,
  BadData,
  UnsupportedMethod,
  UnsupportedProtocol,
  Ok,
};

enum HttpMethod {
  Get,
  Head,
  Post,
  Put,
  Patch,
  Delete,
  Connect,
  Options,
  Trace,
  Unkown
};

enum Version {
  HTTP0_9,
  HTTP1_0,
  HTTP1_1,
};

struct Pair {
  View<char> key;
  View<char> value;
  Pair() : key{0, 0, nullptr}, value{0, 0, nullptr} {}
  Pair(View<char> key, View<char> value) : key{key}, value{value} {}
};

int64_t bytesToInt(View<char> cs);

class HttpParser {
  HttpMethod _method = HttpMethod::Unkown;
  Version _version = Version::HTTP0_9;
  int64_t _contentLength = -1;
  std::optional<View<char>> _path = std::nullopt;
  Buffer<Pair> headers{};
  Buffer<Pair> query{};

public:
  View<char> path();

  HttpMethod method();

  Version version();

  int64_t content_length();

  std::optional<View<char>> find_header(std::string header);
  View<char> parse(View<char> content, ParseResult &result);
};
} // namespace blaze