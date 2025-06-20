#ifndef CHUNKSTREAM_RECEIVER_MEMORY_POOL_H_
#define CHUNKSTREAM_RECEIVER_MEMORY_POOL_H_

#include <cstdint>
#include <vector>
#include <stack>
#include <mutex>

namespace chunkstream {

class MemoryPool {
public:
  MemoryPool(size_t block_size, size_t buffer_size);

  // @return Pointer of reserved buffer, or nullptr if no buffer is available.
  uint8_t* Acquire();

  void Release(uint8_t* ptr);

private:
  std::vector<uint8_t> pool_;
  std::stack<size_t> free_blocks_;  // Indices of available blocks
  std::mutex mutex_;
  
public:
  const size_t BUFFER_SIZE;
  const size_t BLOCK_SIZE;
};

}

#endif