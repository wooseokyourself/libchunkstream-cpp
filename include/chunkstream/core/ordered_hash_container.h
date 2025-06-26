// Copyright (c) 2025 Wooseok Choi
// Licensed under the MIT License - see LICENSE file

#ifndef CHUNKSTREAM_CORE_ORDERED_HASH_CONTAINER_H_
#define CHUNKSTREAM_CORE_ORDERED_HASH_CONTAINER_H_

#include <unordered_map>
#include <list>
#include <mutex>

namespace chunkstream {

template<typename Key, typename Value>
class OrderedHashContainer {
private:
  std::list<std::pair<Key, Value>> ordered_data_;  
  std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> key_to_iterator_;
  mutable std::mutex lock_;
    
public:
  std::pair<Key, Value>& operator[](const int i) {
    return *(ordered_data_.begin() + i);
  }

  // O(1) insertion
  auto& push_back(const Key& key, const Value& value) {
    std::lock_guard<std::mutex> lock(lock_);
    auto& ref = ordered_data_.emplace_back(key, value);
    auto it = std::prev(ordered_data_.end());
    key_to_iterator_[key] = it;
    return ref;
  }

  // O(1) insertion
  auto& push_back(const Key& key, Value&& value) {
    std::lock_guard<std::mutex> lock(lock_);
    auto& ref = ordered_data_.emplace_back(key, std::move(value));
    auto it = std::prev(ordered_data_.end());
    key_to_iterator_[key] = it;
    return ref;
  }
  
  // O(1) in-place construction
  template<typename... Args>
  auto& emplace_back(const Key& key, Args&&... args) {
    std::lock_guard<std::mutex> lock(lock_);
    auto& ref = ordered_data_.emplace_back(
      std::piecewise_construct,
      std::forward_as_tuple(key),
      std::forward_as_tuple(std::forward<Args>(args)...)
    );
    auto it = std::prev(ordered_data_.end());
    key_to_iterator_[key] = it;
    return ref;
  }

  // O(1) front
  const std::pair<Key, Value>& front() const {
    std::lock_guard<std::mutex> lock(lock_);
    return ordered_data_.front();
  }
  
  // O(1) back
  const std::pair<Key, Value>& back() const {
    std::lock_guard<std::mutex> lock(lock_);
    return ordered_data_.back();
  }
  
  // O(1) ~ O(n) search
  Value* find(const Key& key) {
    std::lock_guard<std::mutex> lock(lock_);
    auto it = key_to_iterator_.find(key);
    if (it != key_to_iterator_.end()) {
      return &(it->second->second);
    }
    return nullptr;
  }
  
  // O(1) pop
  void pop_front() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!ordered_data_.empty()) {
      key_to_iterator_.erase(ordered_data_.front().first);
      ordered_data_.pop_front();
    }
  }
  
  // O(n) remove
  void erase(const Key& key) {
    std::lock_guard<std::mutex> lock(lock_);
    auto it = key_to_iterator_.find(key);
    if (it != key_to_iterator_.end()) {
      ordered_data_.erase(it->second);
      key_to_iterator_.erase(it);
    }
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock(lock_);
    return ordered_data_.empty();
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(lock_);
    return ordered_data_.size();
  }
};

}

#endif