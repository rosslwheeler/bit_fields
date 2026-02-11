#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

#include "bit_fields/bitstream.h"

// 8-line direct-mapped cache with 64-byte lines and backing memory.
//
// Address decomposition (32-bit):
//   [  TAG (23 bits)  |  INDEX (3 bits)  |  OFFSET (6 bits)  ]
//   bits 31..9           bits 8..6          bits 5..0
//
// - OFFSET: byte position within the 64-byte cache line (2^6 = 64)
// - INDEX:  selects which of the 8 cache lines (2^3 = 8)
// - TAG:    identifies which memory block is stored in that line

static constexpr uint32_t kLineSize  = 64;
static constexpr uint32_t kNumLines  = 8;
static constexpr uint32_t kDumpBytes = 8;

// Define cache address format using bit_fields library (LittleEndian).
// Fields listed LSB-first: offset(6), index(3), tag(23) = 32 bits total.
static constexpr bit_fields::PacketFormat<3> kCacheAddress{{{
    bit_fields::FieldDef{"offset", 6},
    bit_fields::FieldDef{"index", 3},
    bit_fields::FieldDef{"tag", 23},
}}};

// Decomposed address fields.
struct AddressFields {
  uint32_t tag;
  uint32_t index;
  uint32_t offset;
};

// Parse an address into tag/index/offset using the bit_fields library.
[[nodiscard]] inline AddressFields deserialize_address(uint32_t address) {
  bit_fields::BitReader<bit_fields::WireOrder::LittleEndian> reader(address);
  AddressFields fields{};
  reader.deserialize_into(kCacheAddress, fields.offset, fields.index, fields.tag);
  return fields;
}

// Reconstruct an address from tag/index/offset using the bit_fields library.
[[nodiscard]] inline uint32_t serialize_address(uint32_t tag, uint32_t index, uint32_t offset) {
  uint32_t address = 0;
  bit_fields::BitWriter<bit_fields::WireOrder::LittleEndian> writer(address);
  writer.serialize(kCacheAddress, offset, index, tag);
  return address;
}

struct CacheLine {
  bool valid   = false;
  bool dirty   = false;
  uint32_t tag = 0;
  std::array<std::byte, kLineSize> data{};
};

// Sparse backing memory: maps line-aligned addresses to 64-byte blocks.
// Returns a zero-filled block on first access (simulates cleared DRAM).
class BackingMemory {
public:
  void write_byte(uint32_t address, std::byte value);
  [[nodiscard]] std::byte read_byte(uint32_t address);

  // Line-level transfers (logs the data being moved).
  [[nodiscard]] std::array<std::byte, kLineSize> fetch_line(uint32_t line_addr);
  void store_line(uint32_t line_addr, const std::array<std::byte, kLineSize>& data);

private:
  std::unordered_map<uint32_t, std::array<std::byte, kLineSize>> storage_;

  // Raw access (no logging).
  [[nodiscard]] std::array<std::byte, kLineSize>& line_at(uint32_t line_addr);
};

class DirectMapCache {
public:
  explicit DirectMapCache(BackingMemory& memory);

  [[nodiscard]] std::byte read(uint32_t address);
  void write(uint32_t address, std::byte value);
  void flush();
  void print_stats() const;
  void dump() const;

private:
  std::array<CacheLine, kNumLines> lines_{};
  BackingMemory& memory_;
  uint32_t hits_   = 0;
  uint32_t misses_ = 0;

  void evict(CacheLine& line, uint32_t index);
  void print_access(
      const char* op, uint32_t address, uint32_t tag, uint32_t index, uint32_t offset) const;
};