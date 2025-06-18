#ifndef CHUNKSTREAM_CORE_ORDERED_HASH_CONTAINER_H_
#define CHUNKSTREAM_CORE_ORDERED_HASH_CONTAINER_H_

#include <unordered_map>
#include <deque>

namespace chunkstream {

template<typename Key, typename Value>
class OrderedHashContainer {
private:
  std::deque<std::pair<Key, Value>> ordered_data_;  
  std::unordered_map<Key, typename std::deque<std::pair<Key, Value>>::iterator> key_to_iterator_;
    
public:
  // O(1) insertion
  void push_back(const Key& key, const Value& value) {
      ordered_data_.emplace_back(key, value);
      auto it = std::prev(ordered_data_.end());
      key_to_iterator_[key] = it;
  }
  
  // O(1) front
  const std::pair<Key, Value>& front() const {
      return ordered_data_.front();
  }
  
  // O(1) back
  const std::pair<Key, Value>& back() const {
      return ordered_data_.back();
  }
  
  // O(1) ~ O(n) search
  Value* find(const Key& key) {
    auto it = key_to_iterator_.find(key);
    if (it != key_to_iterator_.end()) {
      return &(it->second->second);
    }
    return nullptr;
  }
  
  // O(1) pop
  void pop_front() {
    if (!ordered_data_.empty()) {
      key_to_iterator_.erase(ordered_data_.front().first);
      ordered_data_.pop_front();
    }
  }
  
  // O(n) remove
  void erase(const Key& key) {
    auto it = key_to_iterator_.find(key);
    if (it != key_to_iterator_.end()) {
      ordered_data_.erase(it->second);
      key_to_iterator_.erase(it);
    }
  }

  bool empty() const {
    return ordered_data_.empty();
  }

  size_t size() const {
    return ordered_data_.size();
  }
};

}

#endif