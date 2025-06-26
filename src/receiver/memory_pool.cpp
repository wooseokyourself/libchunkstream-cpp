// Copyright (c) 2025 Wooseok Choi
// Licensed under the MIT License - see LICENSE file

#include "chunkstream/receiver/memory_pool.h"

namespace chunkstream {

MemoryPool::MemoryPool(size_t block_size, size_t buffer_size) 
  : BUFFER_SIZE(buffer_size), BLOCK_SIZE(block_size) {
  pool_.resize(BLOCK_SIZE * BUFFER_SIZE);
  
  for (int i = buffer_size - 1; i >= 0; --i) {
    free_blocks_.push(i);
  }
}

// @return Pointer of reserved buffer, or nullptr if no buffer is available.
uint8_t* MemoryPool::Acquire() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (free_blocks_.empty()) {
    return nullptr;
  }
  
  size_t idx = free_blocks_.top();
  free_blocks_.pop();
  
  return pool_.data() + (idx * BLOCK_SIZE);
}

void MemoryPool::Release(uint8_t* ptr) {
  if (ptr == nullptr) return;
  
  uint8_t* pool_start = pool_.data();
  size_t offset = ptr - pool_start;
  size_t idx = offset / BLOCK_SIZE;
  
  // Checks validation
  if (offset % BLOCK_SIZE != 0 || idx >= BUFFER_SIZE) {
    return;
  }
  
  std::lock_guard<std::mutex> lock(mutex_);
  free_blocks_.push(idx);
}

}