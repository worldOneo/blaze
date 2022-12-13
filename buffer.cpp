#include "buffer.hh"

namespace blaze {
bool equalIgnoreCase(View<char> first, std::string second) {
  if (second.size() != first.length())
    return false;
  for (size_t i = 0; i < first.length(); i++) {
    char a = first.get(i);
    if (a >= 'A' && a <= 'Z') {
      a = 'a' + a - 'A';
    }
    char b = second[i];
    if (b >= 'A' && b <= 'Z') {
      b = 'a' + b - 'A';
    }
    if (b != a) {
      return false;
    }
  }
  return true;
}

bool equal(View<char> me, std::string other) {
  if (other.size() != me.length())
    return false;
  for (size_t i = 0; i < me.length(); i++) {
    if (other[i] != me.get(i)) {
      return false;
    }
  }
  return true;
}
} // namespace blaze