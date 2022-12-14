#pragma once
#include <memory>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace blaze {
template <typename Store> class Buffer;

template <typename Store> class View {
private:
  size_t offset;
  size_t limit;
  Buffer<Store> *backing;

public:
  View(size_t offset, size_t limit, Buffer<Store> *backing)
      : offset{offset}, limit{limit}, backing{backing} {}

  __uint8_t get(size_t index) { return backing->get(index + offset); }

  size_t length() { return limit - offset; }

  View<Store> sub(size_t start, size_t end) {
    return View{offset + start, offset + end, backing};
  }

  bool equal(View<Store> &other) {
    if (other.length() != length())
      return false;
    for (size_t i = 0; i < length(); i++) {
      if (other.get(i) != get(i)) {
        return false;
      }
    }
    return true;
  }
};

template <typename Store> struct store_deleter {
  void operator()(Store data[]) { free(data); }
};

template <typename Store> class Buffer {
private:
  size_t len;
  std::vector<Store> data;

public:
  Buffer() : len{0}, data{} { data.reserve(16); }

  void write(const Store data) {
    this->data.reserve(len + 1);
    this->data[len] = data;
    len++;
  }

  void write(const Store *data, size_t n) {
    this->data.reserve(len + n);
    for (int i = 0; i < n; i++) {
      this->data[len + i] = data[i];
    }
    len += n;
  }

  void write_all(View<Store> data) {
    this->data.reserve(len + data.length());
    for (int i = 0; i < data.length(); i++) {
      this->data[len + i] = data.get(i);
    }
    len += data.length();
  }

  void reset() { this->len = 0; }

  Store get(size_t index) { return data[index]; }

  View<Store> view() { return View<Store>(0, len, this); }

  size_t length() { return len; }
};

bool equal(View<char> me, std::string other);
bool equalIgnoreCase(View<char> first, std::string second);
} // namespace blaze
