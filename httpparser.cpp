#include "buffer.hh"
#include <optional>
#include <string>

namespace blaze {
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
};

HttpMethod requestMethod(View<char> data) {
  size_t l = data.length();
  switch (l) {
  case 3: {
    if (equal(data, "GET")) {
      return HttpMethod::Get;
    } else if (equal(data, "PUT")) {
      return HttpMethod::Put;
    }
    break;
  }
  case 4: {
    if (equal(data, "HEAD")) {
      return HttpMethod::Head;
    }
    if (equal(data, "POST")) {
      return HttpMethod::Post;
    }
    break;
  }
  case 5: {
    if (equal(data, "PATCH")) {
      return HttpMethod::Patch;
    } else if (equal(data, "TRACE")) {
      return HttpMethod::Trace;
    }
    break;
  }
  case 6: {
    if (equal(data, "DELETE")) {
      return HttpMethod::Delete;
    }
    break;
  }
  case 7: {
    if (equal(data, "CONNECT")) {
      return HttpMethod::Connect;
    }
    if (equal(data, "OPTIONS")) {
      return HttpMethod::Options;
    }
  }
  }
  return HttpMethod::Unkown;
}

enum ParseResult {
  IncompleteData,
  BadData,
  UnsupportedMethod,
  UnsupportedProtocol,
  Ok,
};

bool isHorSpace(char c) { return c == ' ' || c == '\t'; }

int64_t bytesToInt(View<char> cs) {
  int64_t a{};
  for (size_t i = 0; i < cs.length(); i++) {
    a *= 10;
    a += cs.get(i) - '0';
  }
  return a;
}

class HttpParser {
  HttpMethod _method = HttpMethod::Unkown;
  Version _version = Version::HTTP0_9;
  int64_t _contentLength = -1;
  std::optional<View<char>> _path = std::nullopt;
  Buffer<Pair> headers{};
  Buffer<Pair> query{};

public:
  View<char> path() { return _path.value_or(View<char>(0, 0, nullptr)); }

  HttpMethod method() { return _method; }

  int64_t content_length() { return _contentLength; }

  std::optional<View<char>> find_header(std::string header) {
    for (size_t i = 0; i < headers.length(); i++) {
      if (equalIgnoreCase(headers.get(i).key, header)) {
        return headers.get(i).value;
      }
    }
    return std::nullopt;
  }

#define exit_incomplete(x)                                                     \
  if (x) {                                                                     \
    result = ParseResult::IncompleteData;                                      \
    return content;                                                            \
  }

#define skip_while(x)                                                          \
  while (reader < length && x) {                                               \
    reader++;                                                                  \
  }                                                                            \
  exit_incomplete(reader == length)

#define skip_space skip_while(isHorSpace(content.get(reader)))

#define skip_nospace skip_while(!isHorSpace(content.get(reader)))
  View<char> parse(View<char> content, ParseResult &result) {
    // Min request: GET / HTTP/X.X\r\n\r\n
    if (content.length() < 18) {
      result = ParseResult::IncompleteData;
      return content;
    }
    _contentLength = -1;
    query.reset();
    headers.reset();
    size_t length = content.length();
    size_t reader = 0;
    size_t methodStart = reader;
    skip_nospace;
    _method = requestMethod(content.sub(methodStart, reader));
    exit_incomplete(_method == HttpMethod::Unkown);
    skip_space;
    size_t queryPathStart = reader;
    skip_while(!isHorSpace(content.get(reader)) && content.get(reader) != '?');
    _path = content.sub(queryPathStart, reader);
    if (content.get(reader) == '?' && reader < length - 1) {
      while (!isHorSpace(content.get(reader))) {
        reader++;
        size_t paramNameStart = reader;
        skip_while(!isHorSpace(content.get(reader)) &&
                   content.get(reader) != '=');
        size_t paramNameEnd = reader;
        reader++;
        size_t paramValStart = reader;
        skip_while(!isHorSpace(content.get(reader)) &&
                   content.get(reader) != '&');
        size_t paramValEnd = reader;
        View<char> key = content.sub(paramNameStart, paramNameEnd);
        View<char> val = content.sub(paramValStart, paramValEnd);
        query.write(Pair{.key = key, .value = val});
      }
    }
    while (reader < length && !isHorSpace(content.get(reader))) {
      reader++;
    }
    exit_incomplete(length < reader + 6);
    reader++;
    if (!equal(content.sub(reader, reader + 5), "HTTP/")) {
      result = ParseResult::UnsupportedProtocol;
      return content;
    }
    reader += 5;
    size_t vStart = reader;
    while (reader < length && content.get(reader) != '\r') {
      reader++;
    };
    exit_incomplete(length < reader + 1);
    if (content.get(reader) != '\r' || content.get(reader + 1) != '\n') {
      result = ParseResult::BadData;
      return content;
    }
    size_t vEnd = reader;
    View<char> version = content.sub(vStart, vEnd);
    if (equal(version, "0.9")) {
      this->_version = Version::HTTP0_9;
    } else if (equal(version, "1.0")) {
      this->_version = Version::HTTP1_0;
    } else if (equal(version, "1.1")) {
      this->_version = Version::HTTP1_1;
    } else {
      result = ParseResult::UnsupportedMethod;
      return content;
    }
    reader += 2;
    while (reader < length && content.get(reader) != '\r') {
      size_t paramNameStart = reader;
      skip_while(content.get(reader) != ':');
      size_t paramNameEnd = reader;
      reader++;
      skip_space;
      size_t paramValStart = reader;
      skip_while(content.get(reader) != '\r');
      size_t paramValEnd = reader;
      reader++;
      exit_incomplete(reader == length);
      if (content.get(reader) != '\n') {
        result = ParseResult::BadData;
        return content;
      }
      reader++;
      View<char> name = content.sub(paramNameStart, paramNameEnd);
      View<char> val = content.sub(paramValStart, paramValEnd);
      if (equalIgnoreCase(name, "Content-Length")) {
        _contentLength = bytesToInt(val);
      }
      headers.write(Pair{.key = name, .value = val});
    }
    exit_incomplete(length < reader + 2);
    if (!equal(content.sub(reader, reader + 2), "\r\n")) {
      result = ParseResult::BadData;
      return content;
    }
    reader += 2;
    result = ParseResult::Ok;
    return content.sub(reader, content.length());
  }
#undef skip_while
#undef skip_space
#undef skip_nospace
#undef quick_exit
};

} // namespace blaze