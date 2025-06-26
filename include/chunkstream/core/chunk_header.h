// Copyright (c) 2025 Wooseok Choi
// Licensed under the MIT License - see LICENSE file

#ifndef CHUNKSTREAM_CORE_CHUNK_HEADER_H_
#define CHUNKSTREAM_CORE_CHUNK_HEADER_H_

#include <cstdint>
#ifdef __linux__
#include <cstddef>
#endif

namespace chunkstream {

struct ChunkHeader {
  uint32_t id;                // Original data ID
  uint32_t total_size;        // Total size of original data
  uint16_t total_chunks;      // Total number of chunks
  uint16_t chunk_index;       // Chunk sequence number (starting from 0)
  uint32_t chunk_size;        // Actual data size in this chunk
  uint16_t transmission_type; // 0: INIT | 1: RESEND
};

const size_t CHUNKHEADER_SIZE = sizeof(ChunkHeader);

void HostToNetwork(ChunkHeader*);

void NetworkToHost(ChunkHeader*);

ChunkHeader HostToNetwork(const ChunkHeader&);

ChunkHeader NetworkToHost(const ChunkHeader&);

}

#endif