#include "cache_example.h"

#include <iomanip>
#include <iostream>

static std::ostream& print_hex(uint32_t value, int width) {
  return std::cout << "0x" << std::hex << std::setfill('0') << std::setw(width) << value
                   << std::dec;
}

static std::ostream& print_hex_byte(std::byte value) {
  return print_hex(std::to_integer<int>(value), 2);
}

static std::ostream& print_line_data(const std::array<std::byte, kLineSize>& data) {
  std::cout << "[";
  for (uint32_t byte_idx = 0; byte_idx < kDumpBytes; byte_idx++) {
    if (byte_idx > 0) {
      std::cout << " ";
    }
    print_hex_byte(data[byte_idx]);
  }
  return std::cout << " ...]";
}

// --- BackingMemory ---

std::array<std::byte, kLineSize>& BackingMemory::line_at(uint32_t line_addr) {
  return storage_[line_addr];  // default-inserts zero-filled array
}

void BackingMemory::write_byte(uint32_t address, std::byte value) {
  auto [tag, index, offset]  = deserialize_address(address);
  uint32_t line_addr         = address - offset;
  std::byte old_value        = line_at(line_addr)[offset];
  line_at(line_addr)[offset] = value;
  std::cout << "  MEM WRITE addr=";
  print_hex(address, 8) << "  old=";
  print_hex_byte(old_value) << "  new=";
  print_hex_byte(value) << "\n";
}

std::byte BackingMemory::read_byte(uint32_t address) {
  auto [tag, index, offset] = deserialize_address(address);
  uint32_t line_addr        = address - offset;
  std::byte value           = line_at(line_addr)[offset];
  std::cout << "  MEM READ  addr=";
  print_hex(address, 8) << "  value=";
  print_hex_byte(value) << "\n";
  return value;
}

std::array<std::byte, kLineSize> BackingMemory::fetch_line(uint32_t line_addr) {
  auto& line_data = line_at(line_addr);
  std::cout << "  MEM FETCH line=";
  print_hex(line_addr, 8) << "  data=";
  print_line_data(line_data) << "\n";
  return line_data;
}

void BackingMemory::store_line(uint32_t line_addr, const std::array<std::byte, kLineSize>& data) {
  auto& old_data = line_at(line_addr);
  std::cout << "  MEM STORE line=";
  print_hex(line_addr, 8) << "  old=";
  print_line_data(old_data) << "  new=";
  print_line_data(data) << "\n";
  old_data = data;
}

// --- DirectMapCache ---

DirectMapCache::DirectMapCache(BackingMemory& memory) : memory_(memory) {}

std::byte DirectMapCache::read(uint32_t address) {
  auto [tag, index, offset] = deserialize_address(address);
  CacheLine& line           = lines_[index];
  uint32_t line_addr        = address - offset;

  print_access("READ", address, tag, index, offset);

  if ((line.valid) && (line.tag == tag)) {
    std::cout << "  -> HIT  value=";
    print_hex_byte(line.data[offset]) << "\n";
    hits_++;
    return line.data[offset];
  }

  // Miss: evict if needed, then fetch from backing memory.
  std::cout << "  -> MISS";
  if (line.valid) {
    evict(line, index);
  }
  std::cout << " (fetching line from memory)\n";
  misses_++;

  // Load the line from backing memory.
  line.data  = memory_.fetch_line(line_addr);
  line.valid = true;
  line.dirty = false;
  line.tag   = tag;

  return line.data[offset];
}

void DirectMapCache::write(uint32_t address, std::byte value) {
  auto [tag, index, offset] = deserialize_address(address);
  CacheLine& line           = lines_[index];
  uint32_t line_addr        = address - offset;

  print_access("WRITE", address, tag, index, offset);

  if ((line.valid) && (line.tag == tag)) {
    std::cout << "  -> HIT  writing ";
    print_hex_byte(value) << "\n";
    hits_++;
  } else {
    std::cout << "  -> MISS";
    if (line.valid) {
      evict(line, index);
    }
    std::cout << " (allocating line)\n";
    misses_++;

    // Fetch the line from backing memory first (write-allocate).
    line.data  = memory_.fetch_line(line_addr);
    line.valid = true;
    line.tag   = tag;
  }

  line.data[offset] = value;
  line.dirty        = true;
}

void DirectMapCache::flush() {
  std::cout << "\n--- Flushing cache ---\n";
  for (uint32_t line_num = 0; line_num < kNumLines; line_num++) {
    CacheLine& line = lines_[line_num];
    if ((line.valid) && (line.dirty)) {
      uint32_t line_addr = serialize_address(line.tag, line_num, 0);
      std::cout << "  Writeback line " << line_num << " (tag=";
      print_hex(line.tag, 6) << ") to memory addr=";
      print_hex(line_addr, 8) << "\n";
      memory_.store_line(line_addr, line.data);
      line.dirty = false;
    }
  }
}

void DirectMapCache::print_stats() const {
  std::cout << "\n=== Cache Stats ===\n"
            << "Hits:   " << hits_ << "\n"
            << "Misses: " << misses_ << "\n"
            << "Total:  " << (hits_ + misses_) << "\n";
  if ((hits_ + misses_) > 0) {
    double rate = 100.0 * hits_ / (hits_ + misses_);
    std::cout << "Hit rate: " << std::fixed << std::setprecision(1) << rate << "%\n";
  }
}

void DirectMapCache::dump() const {
  std::cout << "\n=== Cache State ===\n";
  std::cout << "Line  Valid  Dirty  Tag         First 8 bytes\n";
  std::cout << "----  -----  -----  ----------  -------------------------\n";
  for (uint32_t line_num = 0; line_num < kNumLines; line_num++) {
    const CacheLine& line = lines_[line_num];
    if (line.valid) {
      std::cout << "  " << line_num << "     Y      ";
    } else {
      std::cout << "  " << line_num << "     N      ";
    }
    if (line.dirty) {
      std::cout << "Y    ";
    } else {
      std::cout << "N    ";
    }
    if (line.valid) {
      print_hex(line.tag, 6) << "     ";
      print_line_data(line.data);
    } else {
      std::cout << "  ----       --";
    }
    std::cout << "\n";
  }
}

void DirectMapCache::evict(CacheLine& line, uint32_t index) {
  uint32_t evict_addr = serialize_address(line.tag, index, 0);
  std::cout << " (evicting tag=";
  print_hex(line.tag, 6);
  if (line.dirty) {
    std::cout << ", DIRTY writeback to ";
    print_hex(evict_addr, 8) << ")\n";
    memory_.store_line(evict_addr, line.data);
  } else {
    std::cout << ")";
  }
  line.valid = false;
  line.dirty = false;
}

void DirectMapCache::print_access(
    const char* op, uint32_t address, uint32_t tag, uint32_t index, uint32_t offset) const {
  std::cout << op << " addr=";
  print_hex(address, 8) << "  tag=";
  print_hex(tag, 6) << "  index=" << index << "  offset=" << offset << "\n";
}

// --- main ---

int main() {
  BackingMemory memory;
  DirectMapCache cache(memory);

  std::cout << "=== 8-Line Direct-Mapped Cache with Backing Memory ===\n";
  std::cout << "Line size: " << kLineSize << " bytes\n";
  std::cout << "Lines:     " << kNumLines << "\n";
  std::cout << "Total:     " << (kLineSize * kNumLines) << " bytes\n";
  std::cout << "Address decomposition via bit_fields library\n\n";

  // Pre-load some data into backing memory so reads return meaningful values.
  memory.write_byte(0x0000, std::byte{0x11});
  memory.write_byte(0x0001, std::byte{0x22});
  memory.write_byte(0x0045, std::byte{0x33});
  memory.write_byte(0x0200, std::byte{0x44});
  memory.write_byte(0x0203, std::byte{0x55});

  std::cout << "--- Scenario 1: Basic writes to different lines ---\n";
  cache.write(0x0000, std::byte{0xAA});  // index=0, tag=0
  cache.write(0x0045, std::byte{0xBB});  // index=1, tag=0 (offset=5 within line 1)
  cache.write(0x0080, std::byte{0xCC});  // index=2, tag=0
  cache.write(0x01C0, std::byte{0xDD});  // index=7, tag=0

  std::cout << "\n--- Scenario 2: Read hits (same lines) ---\n";
  (void) cache.read(0x0000);  // hit, index=0 -> returns 0xAA (from cache, not memory)
  (void) cache.read(
      0x0001);  // hit, index=0, offset=1 -> returns 0x22 (fetched from memory on first miss)
  (void) cache.read(0x0045);  // hit, index=1 -> returns 0xBB (written to cache)

  std::cout << "\n--- Scenario 3: Conflict miss (same index, different tag) ---\n";
  // 0x0200 -> index=0, tag=1 -> conflicts with 0x0000 which is at index=0, tag=0
  // Dirty writeback of the 0x0000 line occurs, then 0x0200 line is fetched.
  (void) cache.read(0x0200);  // miss, evicts index=0 (dirty writeback), fetches new line

  std::cout << "\n--- Scenario 4: Re-read evicted address ---\n";
  // 0x0000 was evicted, so this is a miss. But the dirty writeback saved 0xAA
  // to backing memory, so after re-fetching the line we get 0xAA back.
  std::byte val = cache.read(0x0000);  // miss, re-fetches from memory
  std::cout << "  Verified: value at 0x0000 = ";
  print_hex_byte(val);
  if (val == std::byte{0xAA}) {
    std::cout << " (writeback preserved!)";
  } else {
    std::cout << " (ERROR)";
  }
  std::cout << "\n";

  std::cout << "\n--- Scenario 5: Same line, different offset ---\n";
  // 0x0203 -> index=0, tag=1. But index=0 now holds tag=0 (from scenario 4).
  // This is a conflict miss again. After fetch, offset=3 has 0x55 from memory.
  val = cache.read(0x0203);
  std::cout << "  Verified: value at 0x0203 = ";
  print_hex_byte(val);
  if (val == std::byte{0x55}) {
    std::cout << " (correct from memory)";
  } else {
    std::cout << " (ERROR)";
  }
  std::cout << "\n";

  // Flush remaining dirty lines back to memory.
  cache.flush();

  cache.dump();
  cache.print_stats();
  std::cout << "\n=== End of Simulation ===\n";

  return 0;
}