#include "chunkstream/core/chunk_header.h"

#include <asio.hpp>

namespace chunkstream {

void HostToNetwork(ChunkHeader* header) {
  header->id = htonl(header->id);
  header->total_size = htonl(header->total_size);
  header->total_chunks = htons(header->total_chunks);
  header->chunk_index = htons(header->chunk_index);
  header->chunk_size = htonl(header->chunk_size);
  header->transmission_type = htons(header->transmission_type); 
}

void NetworkToHost(ChunkHeader* header) {
  header->id = ntohl(header->id);
  header->total_size = ntohl(header->total_size);
  header->total_chunks = ntohs(header->total_chunks);
  header->chunk_index = ntohs(header->chunk_index);
  header->chunk_size = ntohl(header->chunk_size);
  header->transmission_type = ntohs(header->transmission_type); 
}

ChunkHeader HostToNetwork(const ChunkHeader& header) {
  return {
    htonl(header.id), 
    htonl(header.total_size), 
    htons(header.total_chunks), 
    htons(header.chunk_index), 
    htonl(header.chunk_size), 
    htons(header.transmission_type)
  };
}

ChunkHeader NetworkToHost(const ChunkHeader& header) {
  return {
    ntohl(header.id), 
    ntohl(header.total_size), 
    ntohs(header.total_chunks), 
    ntohs(header.chunk_index), 
    ntohl(header.chunk_size), 
    ntohs(header.transmission_type)
  };
}

}