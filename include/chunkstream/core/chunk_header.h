#ifndef CHUNKSTREAM_CORE_CHUNK_HEADER_H_
#define CHUNKSTREAM_CORE_CHUNK_HEADER_H_

#include <cstdint>

namespace chunkstream {

struct ChunkHeader {
  uint32_t id;                // 원본 데이터 ID
  uint32_t total_size;        // 원본 데이터 전체 크기
  uint16_t total_chunks;      // 전체 청크 개수
  uint16_t chunk_index;       // 청크 순서 (0부터)
  uint32_t chunk_size;        // 실제 데이터 크기
  uint16_t transmission_type; // 0: INIT | 1: RESEND
};

const size_t CHUNKHEADER_SIZE = sizeof(ChunkHeader);

void HostToNetwork(ChunkHeader*);

void NetworkToHost(ChunkHeader*);

ChunkHeader HostToNetwork(const ChunkHeader&);

ChunkHeader NetworkToHost(const ChunkHeader&);

}

#endif