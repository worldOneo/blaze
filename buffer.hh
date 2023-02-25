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

  Store *data() { return this->backing->data() + offset; }
};

template <typename Store> struct store_deleter {
  void operator()(Store data[]) { free(data); }
};

template <typename Store> class Buffer {
private:
  size_t len{0};
  std::vector<Store> _data{};

public:
  Buffer() {}

  void write(const Store data) {
    if(this->_data.size() <= len) {
      this->reserve(1);
    }
    this->_data[len] = data;
    this->len += 1;
  }

  void write(const Store *data, size_t n) {
    this->_data.reserve(len + n);
    _data.insert(_data.begin() + len, data, data + n);
    this->len += n;
  }

  void write_all(View<Store> data) { write(data.data(), data.length()); }

  void mark_ready(size_t size) { len += size; }

  void reserve(size_t size) {
    if (size > _data.size() - len) {
      this->_data.resize(_data.size() + size);
    }
  }

  void reset() {
    this->_data.resize(0);
    this->len = 0;
  }

  Store get(size_t index) { return _data[index]; }
  Store *ref(size_t index) { return &_data.data()[index]; }

  View<Store> view() { return View<Store>(0, len, this); }

  size_t length() { return len; }

  Store *data() { return _data.data(); }
};

bool equal(View<char> me, std::string other);
bool equalIgnoreCase(View<char> first, std::string second);
} // namespace blaze
