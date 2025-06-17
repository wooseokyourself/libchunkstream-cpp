#ifndef CHUNKSTREAM_CORE_CHUNK_HEADER_H_
#define CHUNKSTREAM_CORE_CHUNK_HEADER_H_

#include <cstdint>

namespace chunkstream {

struct ChunkHeader {
  uint32_t buffer_index;    // 버퍼 인덱스
  uint32_t total_size;      // 원본 데이터 전체 크기
  uint16_t total_chunks;    // 전체 청크 개수
  uint16_t chunk_index;     // 청크 순서 (0부터)
  uint32_t chunk_size;      // 실제 데이터 크기
};

const size_t CHUNKHEADER_SIZE = sizeof(ChunkHeader);

}

#endif