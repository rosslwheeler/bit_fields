#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

#include "bit_fields/bitstream.h"

using namespace bit_fields;

constexpr std::size_t kNumPackets        = 100;
constexpr std::size_t kFieldsPerPacket   = 100;
constexpr std::size_t kDefaultIterations = 100;
constexpr unsigned int kRandomSeed       = 42;
constexpr unsigned int kRandomSeed2      = 43;  // Different seed for benchmark 2 values

// Pre-generated field widths using Python: random.seed(42), random.randint(1, 64)
// Total bits: 3061, Bytes needed: 383
constexpr std::array<std::size_t, kFieldsPerPacket> kFieldWidths = {{
    15, 4,  36, 32, 29, 18, 14, 12, 55, 5,  4,  12, 28, 30, 4,  26, 54, 29, 58, 36,
    1,  21, 55, 44, 36, 20, 28, 44, 14, 12, 49, 13, 46, 45, 34, 6,  59, 16, 49, 11,
    38, 47, 25, 9,  6,  30, 38, 11, 30, 13, 49, 36, 59, 47, 21, 48, 46, 27, 35, 10,
    22, 32, 21, 60, 49, 35, 29, 42, 8,  30, 5,  41, 52, 35, 9,  28, 41, 28, 64, 51,
    59, 19, 34, 18, 32, 34, 55, 52, 47, 29, 18, 64, 12, 7,  15, 20, 21, 55, 9,  50,
}};

constexpr std::size_t kTotalBits = [] {
  std::size_t sum = 0;
  for (auto w : kFieldWidths)
    sum += w;
  return sum;
}();
constexpr std::size_t kBytesPerPacket = (kTotalBits + 7) / 8;

// =============================================================================
// Benchmark 1: Manual bit operations (read_bits_runtime / write_bits_runtime)
// =============================================================================

// Generate random values for the shared field widths
std::array<std::uint64_t, kFieldsPerPacket> generate_values(std::mt19937_64& rng) {
  std::array<std::uint64_t, kFieldsPerPacket> values{};
  for (std::size_t i = 0; i < kFieldsPerPacket; ++i) {
    std::size_t width     = kFieldWidths[i];
    std::uint64_t max_val = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
    std::uniform_int_distribution<std::uint64_t> dist(0, max_val);
    values[i] = dist(rng);
  }
  return values;
}

void run_manual_bits_benchmark(std::size_t iterations) {
  // Generate values for each packet using seed 42
  std::mt19937_64 rng(kRandomSeed);
  std::vector<std::array<std::uint64_t, kFieldsPerPacket>> packet_values(kNumPackets);
  for (auto& values : packet_values) {
    values = generate_values(rng);
  }

  // Allocate buffers for all packets
  std::vector<std::vector<std::byte>> buffers(kNumPackets);
  for (std::size_t i = 0; i < kNumPackets; ++i) {
    buffers[i].resize(kBytesPerPacket, std::byte{0});
  }

  double total_write_ms = 0.0;
  double total_read_ms  = 0.0;

  for (std::size_t iter = 0; iter < iterations; ++iter) {
    // Clear buffers
    for (auto& buf : buffers) {
      std::fill(buf.begin(), buf.end(), std::byte{0});
    }

    // Write phase
    auto write_start = std::chrono::high_resolution_clock::now();
    for (std::size_t p = 0; p < kNumPackets; ++p) {
      NetworkBitWriter writer{std::span<std::byte>(buffers[p])};
      for (std::size_t i = 0; i < kFieldsPerPacket; ++i) {
        writer.write_bits_runtime(kFieldWidths[i], packet_values[p][i]);
      }
    }
    auto write_end = std::chrono::high_resolution_clock::now();

    // Read phase with verification
    auto read_start = std::chrono::high_resolution_clock::now();
    for (std::size_t p = 0; p < kNumPackets; ++p) {
      NetworkBitReader reader{std::span<const std::byte>(buffers[p])};
      for (std::size_t i = 0; i < kFieldsPerPacket; ++i) {
        auto val = reader.read_bits_runtime(kFieldWidths[i]);
        if (val != packet_values[p][i]) {
          std::cerr << "Manual bits verification FAILED!\n";
          std::exit(1);
        }
      }
    }
    auto read_end = std::chrono::high_resolution_clock::now();

    total_write_ms += std::chrono::duration<double, std::milli>(write_end - write_start).count();
    total_read_ms += std::chrono::duration<double, std::milli>(read_end - read_start).count();
  }

  std::cout << "Benchmark 1: Manual Bit Operations (read/write_bits_runtime)\n";
  std::cout << "-------------------------------------------------------------\n";
  std::cout << "  Results (averaged over " << iterations << " iterations):\n";
  std::cout << "    Write: " << (total_write_ms / iterations) << " ms\n";
  std::cout << "    Read:  " << (total_read_ms / iterations) << " ms\n";
  std::cout << "    Total: " << ((total_write_ms + total_read_ms) / iterations) << " ms\n";
  std::cout << "  Verification: PASSED\n\n";
}

// =============================================================================
// Benchmark 2: Format-based operations (serialize / deserialize)
// =============================================================================

// Build PacketFormat from the shared field widths
constexpr auto make_format_fields() {
  std::array<FieldDef, kFieldsPerPacket> fields{};
  for (std::size_t i = 0; i < kFieldsPerPacket; ++i) {
    fields[i] = FieldDef{"", kFieldWidths[i]};
  }
  return fields;
}

constexpr PacketFormat<kFieldsPerPacket> kBenchFormat{{make_format_fields()}};

// Helper to serialize all 100 values
template <std::size_t... Is>
void serialize_all(NetworkBitWriter& writer,
                   const std::array<std::uint64_t, kFieldsPerPacket>& values,
                   std::index_sequence<Is...>) {
  writer.serialize(kBenchFormat, values[Is]...);
}

void run_format_benchmark(std::size_t iterations) {
  // Generate values for each packet using seed 43 (different from benchmark 1)
  std::mt19937_64 rng(kRandomSeed2);
  std::vector<std::array<std::uint64_t, kFieldsPerPacket>> packet_values(kNumPackets);
  for (auto& values : packet_values) {
    values = generate_values(rng);
  }

  // Allocate buffers
  std::vector<std::vector<std::byte>> buffers(kNumPackets);
  for (std::size_t i = 0; i < kNumPackets; ++i) {
    buffers[i].resize(kBytesPerPacket, std::byte{0});
  }

  double total_serialize_ms   = 0.0;
  double total_deserialize_ms = 0.0;

  for (std::size_t iter = 0; iter < iterations; ++iter) {
    // Clear buffers
    for (auto& buf : buffers) {
      std::fill(buf.begin(), buf.end(), std::byte{0});
    }

    // Serialize phase
    auto ser_start = std::chrono::high_resolution_clock::now();
    for (std::size_t p = 0; p < kNumPackets; ++p) {
      NetworkBitWriter writer{std::span<std::byte>(buffers[p])};
      serialize_all(writer, packet_values[p], std::make_index_sequence<kFieldsPerPacket>{});
    }
    auto ser_end = std::chrono::high_resolution_clock::now();

    // Deserialize phase with verification
    auto deser_start = std::chrono::high_resolution_clock::now();
    for (std::size_t p = 0; p < kNumPackets; ++p) {
      NetworkBitReader reader{std::span<const std::byte>(buffers[p])};
      auto parsed = reader.deserialize(kBenchFormat);

      // Verify all values
      for (std::size_t i = 0; i < kFieldsPerPacket; ++i) {
        if (parsed[i] != packet_values[p][i]) {
          std::cerr << "Format verification FAILED at packet " << p << " field " << i << "\n";
          std::exit(1);
        }
      }
    }
    auto deser_end = std::chrono::high_resolution_clock::now();

    total_serialize_ms += std::chrono::duration<double, std::milli>(ser_end - ser_start).count();
    total_deserialize_ms +=
        std::chrono::duration<double, std::milli>(deser_end - deser_start).count();
  }

  std::cout << "Benchmark 2: Format-Based Operations (serialize / deserialize)\n";
  std::cout << "---------------------------------------------------------------\n";
  std::cout << "  Results (averaged over " << iterations << " iterations):\n";
  std::cout << "    Serialize:   " << (total_serialize_ms / iterations) << " ms\n";
  std::cout << "    Deserialize: " << (total_deserialize_ms / iterations) << " ms\n";
  std::cout << "    Total:       " << ((total_serialize_ms + total_deserialize_ms) / iterations)
            << " ms\n";
  std::cout << "  Verification: PASSED\n\n";
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char* argv[]) {
  std::size_t iterations = kDefaultIterations;

  if (argc > 1) {
    iterations = std::stoul(argv[1]);
    if (iterations == 0) {
      std::cerr << "Error: iterations must be > 0\n";
      return 1;
    }
  }

  std::cout << "bit_fields Benchmark\n";
  std::cout << "====================\n";
  std::cout << "Configuration:\n";
  std::cout << "  Packets: " << kNumPackets << "\n";
  std::cout << "  Fields per packet: " << kFieldsPerPacket << "\n";
  std::cout << "  Field widths: 1-64 bits (random, shared between benchmarks)\n";
  std::cout << "  Total bits per packet: " << kTotalBits << "\n";
  std::cout << "  Iterations: " << iterations << "\n";
  std::cout << "  Random seed (widths & benchmark 1 values): " << kRandomSeed << "\n";
  std::cout << "  Random seed (benchmark 2 values): " << kRandomSeed2 << "\n\n";

  run_manual_bits_benchmark(iterations);
  run_format_benchmark(iterations);

  return 0;
}