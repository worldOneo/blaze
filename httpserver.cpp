#include "httpserver.hh"
#include "iostream"

namespace blaze {
void HttpResponse::reset() {
  while (!claimed.empty()) {
    auto buf = claimed.back();
    buf->reset();
    buffs->put(buf);
    claimed.pop_back();
  }
  body.reset();
  headers.reset();
}

void HttpResponse::write_body(View<char> data) { body.write_all(data); }

void HttpResponse::add_header(View<char> key, View<char> value) {
  headers.write(Pair(key, value));
}

Buffer<char> *HttpResponse::buffer() {
  Buffer<char> *buf = buffs->get();
  claimed.push_back(buf);
  return buf;
};

std::string status_text(uint16_t status) {
  switch (status) {
  // 1xx
  case 100:
    return "Continue";
  case 101:
    return "Switching Protocols";
  // 2xx
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 202:
    return "Accepted";
  case 204:
    return "No Content";
  case 205:
    return "Reset Content";
  case 206:
    return "Partial Content";
  // 3xx
  case 300:
    return "Multiple Choices";
  case 301:
    return "Moved Permanently";
  case 302:
    return "Found";
  case 303:
    return "See Other";
  case 304:
    return "Not Modified";
  case 305:
    return "Use Proxy";
  case 306:
    return "Switch Proxy";
  case 307:
    return "Temporary Redirect";
  case 308:
    return "Permanent Redirect";
  // 4xx
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 402:
    return "Payment Required";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 408:
    return "Request Timeout";
  // 5xx
  case 500:
    return "Internal Server Error";
  case 501:
    return "Not Implemented";
  case 502:
    return "Bad Gateway";
  case 504:
    return "Gateway Timeout";
  case 505:
    return "HTTP Version Not Supported";
  }
  return "Unkown";
}

template <typename T> T reverse(T i) {
  T rev = 0;
  while (i != 0) {
    T digit = i % 10;
    rev = rev * 10 + digit;
    i /= 10;
  }
  return rev;
}

void HttpResponse::render(HttpRequest &req, Buffer<char> *res) {
  char numBuff[12];
  res->write("HTTP/", 5);
  if (req.version() == HTTP0_9) {
    res->write("0.9", 3);
  } else if (req.version() == HTTP1_0) {
    res->write("1.0", 3);
  } else if (req.version() == HTTP1_1) {
    res->write("1.1", 3);
  }
  res->write(' ');
  uint16_t s = _status;
  int i = 0;
  while (s != 0) {
    numBuff[i] = '0' + (s % 10);
    s /= 10;
    i++;
  }
  while (i > 0) {
    i--;
    res->write(numBuff[i]);
  }
  res->write(' ');
  std::string text = status_text(_status);
  res->write(text.c_str(), text.length());
  res->write('\r');
  res->write('\n');
  for (size_t i = 0; i < headers.length(); i++) {
    res->write_all(headers.get(i).key);
    res->write(':');
    res->write(' ');
    res->write_all(headers.get(i).value);
    res->write('\r');
    res->write('\n');
  }
  res->write("Content-Length: ", 16);
  size_t l = body.length();
  while (l != 0) {
    numBuff[i] = '0' + (l % 10);
    l /= 10;
    i++;
  }
  if(i == 0) {
    res->write('0');
  }
  while (i > 0) {
    i--;
    res->write(numBuff[i]);
  }
  res->write('\r');
  res->write('\n');
  res->write('\r');
  res->write('\n');
  res->write_all(body.view());
}

void HttpServer::crash(){};
void HttpServer::boot(){};
Action HttpServer::client_connect(OpenEvent &event) {
  return blaze::Action::NONE;
};
void HttpServer::client_close(CloseEvent &event){};
Action HttpServer::traffic(blaze::DataEvent &event) {
  blaze::HttpParser parser{};
  blaze::ParseResult result{};
  auto body = parser.parse(event.data, result);
  if (result == blaze::ParseResult::IncompleteData) {
    return blaze::Action::NONE;
  }
  if (result != blaze::ParseResult::Ok) {
    return blaze::Action::CLOSE;
  }
  HttpRequest req{parser, body};
  auto res = ress.get();
  res->buffs = &buffs;
  handle(req, res);
  res->render(req, event.response);
  res->reset();
  ress.put(res);
  return blaze::Action::WRITE;
};
} // namespace blaze