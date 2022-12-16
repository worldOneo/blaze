#include <deque>

namespace blaze {
template <typename Data> class Pool {
private:
  std::deque<Data *> pooledItems;

public:
  Data *get() {
    if (pooledItems.empty()) {
      return new Data();
    }
    Data *tmp = pooledItems.front();
    pooledItems.pop_front();
    return tmp;
  }

  void put(Data *item) { pooledItems.push_back(item); }
};
} // namespace blaze