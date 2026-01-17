#include <string_view>

#include "bitstream.h"
#pragma once

/// @file constexpr_bitstream.h
/// @brief Compile-time bit-level parsing and serialization.
///
/// This header provides constexpr-capable BitReader/BitWriter for use cases where
/// packet parsing can be performed entirely at compile time, enabling:
/// - Static validation of packet formats
/// - Zero-runtime-cost packet construction
/// - Compile-time round-trip verification

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace bit_fields {

/// Compile-time field descriptor for packet formats.
struct ConstexprFieldDef {
  std::string_view name;
  std::size_t bit_width;
};

/// Compile-time packet format definition.
/// @tparam N Number of fields in the format.
template <std::size_t N>
struct ConstexprPacketFormat {
  std::array<ConstexprFieldDef, N> fields;

  /// Total bits in this format.
  [[nodiscard]] constexpr std::size_t total_bits() const noexcept {
    std::size_t sum = 0;
    for (const auto& f : fields) {
      sum += f.bit_width;
    }
    return sum;
  }

  /// Total bytes needed (rounded up).
  [[nodiscard]] constexpr std::size_t total_bytes() const noexcept {
    return (total_bits() + 7) / 8;
  }

  /// Find field index by name. Returns std::nullopt if not found.
  [[nodiscard]] constexpr std::optional<std::size_t> field_index(
      std::string_view name) const noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      if (fields[i].name == name) {
        return i;
      }
    }
    return std::nullopt;
  }

  /// Get the bit offset of a field by name.
  /// Returns std::nullopt if field not found.
  [[nodiscard]] constexpr std::optional<std::size_t> bit_offset_of(
      std::string_view name) const noexcept {
    std::size_t offset = 0;
    for (std::size_t i = 0; i < N; ++i) {
      if (fields[i].name == name) {
        return offset;
      }
      offset += fields[i].bit_width;
    }
    return std::nullopt;
  }

  /// Get the bit width of a field by name.
  /// Returns std::nullopt if field not found.
  [[nodiscard]] constexpr std::optional<std::size_t> field_width(
      std::string_view name) const noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      if (fields[i].name == name) {
        return fields[i].bit_width;
      }
    }
    return std::nullopt;
  }
};

/// Result of compile-time parsing.
/// @tparam N Number of fields parsed.
template <std::size_t N>
struct ConstexprParsedPacket {
  std::array<std::uint64_t, N> values{};
  bool valid = false;

  /// Get field value by index.
  [[nodiscard]] constexpr std::uint64_t operator[](std::size_t idx) const noexcept {
    return values[idx];
  }

  /// Check if parsing succeeded.
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return valid; }
};

/// Endianness for wire format interpretation.
enum class ConstexprWireOrder : std::uint8_t {
  BigEndian,  ///< Network byte order (MSB first)
  LittleEndian,  ///< LSB first
};

/// Constexpr bit reader for compile-time parsing.
/// @tparam BufferSize Size of the input buffer in bytes.
/// @tparam Order Wire byte order (default: BigEndian/network order).
template <std::size_t BufferSize, ConstexprWireOrder Order = ConstexprWireOrder::BigEndian>
class ConstexprBitReader {
public:
  /// Construct from a fixed-size byte array.
  constexpr explicit ConstexprBitReader(const std::array<std::byte, BufferSize>& buffer) noexcept
    : buffer_(buffer), bit_offset_(0) {}

  /// Reset read position to the beginning.
  constexpr void reset() noexcept { bit_offset_ = 0; }

  /// Get current bit offset.
  [[nodiscard]] constexpr std::size_t bit_position() const noexcept { return bit_offset_; }

  /// Get current byte offset (rounded down).
  [[nodiscard]] constexpr std::size_t byte_position() const noexcept { return bit_offset_ / 8; }

  /// Check if there are at least n bits remaining.
  [[nodiscard]] constexpr bool has_bits(std::size_t n) const noexcept {
    return (bit_offset_ + n) <= (BufferSize * 8);
  }

  /// Read n bits and return as an integer. Returns nullopt on error.
  [[nodiscard]] constexpr std::optional<std::uint64_t> read_bits(std::size_t n) noexcept {
    if (n == 0 || n > 64 || !has_bits(n)) {
      return std::nullopt;
    }

    std::uint64_t value = 0;

    if constexpr (Order == ConstexprWireOrder::BigEndian) {
      // MSB first: bit 0 of result is the first bit read
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t byte_idx = bit_offset_ / 8;
        std::size_t bit_idx  = 7 - (bit_offset_ % 8);  // MSB = bit 7
        if (std::to_integer<int>(buffer_[byte_idx]) & (1 << bit_idx)) {
          value |= (1ULL << (n - 1 - i));
        }
        ++bit_offset_;
      }
    } else {
      // LSB first: first bit read goes to bit 0 of result
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t byte_idx = bit_offset_ / 8;
        std::size_t bit_idx  = bit_offset_ % 8;  // LSB = bit 0
        if (std::to_integer<int>(buffer_[byte_idx]) & (1 << bit_idx)) {
          value |= (1ULL << i);
        }
        ++bit_offset_;
      }
    }

    return value;
  }

  /// Read exactly N bits (compile-time width).
  template <std::size_t N>
    requires(N > 0 && N <= 64)
  [[nodiscard]] constexpr std::optional<std::uint64_t> read_bits() noexcept {
    return read_bits(N);
  }

  /// Read a single field from a packet format by name.
  /// Seeks to the field's position, reads it, and returns the value.
  /// More efficient than parse() when you only need one field.
  /// Returns nullopt if field not found or not enough bits.
  template <std::size_t N>
  [[nodiscard]] constexpr std::optional<std::uint64_t> read_field(
      const ConstexprPacketFormat<N>& format, std::string_view name) noexcept {
    std::size_t offset = format.bit_offset_of(name);
    std::size_t width  = format.field_width(name);

    if (width == 0 || !has_bits(offset + width)) {
      return std::nullopt;
    }

    // Seek to field position
    bit_offset_ = offset;
    return read_bits(width);
  }

  /// Deserialize an entire packet format, returning all field values.
  /// Fields named "reserved", "padding", or starting with '_' are skipped.
  template <std::size_t N>
  [[nodiscard]] constexpr ConstexprParsedPacket<N> deserialize(
      const ConstexprPacketFormat<N>& format) noexcept {
    ConstexprParsedPacket<N> result{};

    if (!has_bits(format.total_bits())) {
      return result;
    }

    for (std::size_t i = 0; i < N; ++i) {
      if (is_padding_field(format.fields[i].name)) {
        if (!skip_bits(format.fields[i].bit_width)) {
          return result;
        }
        result.values[i] = 0;  // Padding fields return 0
      } else {
        auto val = read_bits(format.fields[i].bit_width);
        if (!val) {
          return result;
        }
        result.values[i] = *val;
      }
    }

    result.valid = true;
    return result;
  }

  /// Skip n bits without reading.
  constexpr bool skip_bits(std::size_t n) noexcept {
    if (!has_bits(n)) {
      return false;
    }
    bit_offset_ += n;
    return true;
  }

  /// Align to the next byte boundary.
  constexpr void align_to_byte() noexcept {
    if (bit_offset_ % 8 != 0) {
      bit_offset_ = ((bit_offset_ / 8) + 1) * 8;
    }
  }

private:
  std::array<std::byte, BufferSize> buffer_;
  std::size_t bit_offset_;
};

/// Constexpr bit writer for compile-time serialization.
/// @tparam BufferSize Size of the output buffer in bytes.
/// @tparam Order Wire byte order (default: BigEndian/network order).
template <std::size_t BufferSize, ConstexprWireOrder Order = ConstexprWireOrder::BigEndian>
class ConstexprBitWriter {
public:
  /// Construct with zero-initialized buffer.
  constexpr ConstexprBitWriter() noexcept : buffer_{}, bit_offset_(0) {}

  /// Get the output buffer.
  [[nodiscard]] constexpr const std::array<std::byte, BufferSize>& data() const noexcept {
    return buffer_;
  }

  /// Get current bit offset.
  [[nodiscard]] constexpr std::size_t bit_position() const noexcept { return bit_offset_; }

  /// Get bytes written so far (rounded up).
  [[nodiscard]] constexpr std::size_t bytes_written() const noexcept {
    return (bit_offset_ + 7) / 8;
  }

  /// Check if there is space for n more bits.
  [[nodiscard]] constexpr bool has_space(std::size_t n) const noexcept {
    return (bit_offset_ + n) <= (BufferSize * 8);
  }

  /// Write n bits from value. Returns false on error.
  constexpr bool write_bits(std::size_t n, std::uint64_t value) noexcept {
    if (n == 0 || n > 64 || !has_space(n)) {
      return false;
    }

    if constexpr (Order == ConstexprWireOrder::BigEndian) {
      // MSB first: bit N-1 of value is written first
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t byte_idx = bit_offset_ / 8;
        std::size_t bit_idx  = 7 - (bit_offset_ % 8);
        if (value & (1ULL << (n - 1 - i))) {
          buffer_[byte_idx] |= std::byte{static_cast<unsigned char>(1 << bit_idx)};
        }
        ++bit_offset_;
      }
    } else {
      // LSB first: bit 0 of value is written first
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t byte_idx = bit_offset_ / 8;
        std::size_t bit_idx  = bit_offset_ % 8;
        if (value & (1ULL << i)) {
          buffer_[byte_idx] |= std::byte{static_cast<unsigned char>(1 << bit_idx)};
        }
        ++bit_offset_;
      }
    }

    return true;
  }

  /// Write exactly N bits (compile-time width).
  template <std::size_t N>
    requires(N > 0 && N <= 64)
  constexpr bool write_bits(std::uint64_t value) noexcept {
    return write_bits(N, value);
  }

  /// Serialize values according to a packet format.
  template <std::size_t N, typename... Args>
    requires(sizeof...(Args) == N)
  constexpr bool serialize(const ConstexprPacketFormat<N>& format, Args... values) noexcept {
    if (!has_space(format.total_bits())) {
      return false;
    }

    std::size_t idx = 0;
    bool ok         = true;
    (
        [&] {
          if (ok) {
            ok = write_bits(format.fields[idx].bit_width, static_cast<std::uint64_t>(values));
          }
          ++idx;
        }(),
        ...);
    return ok;
  }

  /// Skip N bits (write zeros).
  constexpr bool skip_bits(std::size_t n) noexcept {
    if (!has_space(n)) {
      return false;
    }
    bit_offset_ += n;  // Buffer was zero-initialized
    return true;
  }

  /// Pad with zeros to the next byte boundary.
  constexpr void align_to_byte() noexcept {
    if (bit_offset_ % 8 != 0) {
      bit_offset_ = ((bit_offset_ / 8) + 1) * 8;
    }
  }

private:
  std::array<std::byte, BufferSize> buffer_;
  std::size_t bit_offset_;
};

// ============================================================================
// Helper functions
// ============================================================================

/// Create a reader with deduced buffer size.
template <std::size_t N>
constexpr auto make_constexpr_reader(const std::array<std::byte, N>& buffer) {
  return ConstexprBitReader<N>(buffer);
}

/// Create a little-endian reader with deduced buffer size.
template <std::size_t N>
constexpr auto make_constexpr_reader_le(const std::array<std::byte, N>& buffer) {
  return ConstexprBitReader<N, ConstexprWireOrder::LittleEndian>(buffer);
}

// ============================================================================
// Pre-defined constexpr packet formats
// ============================================================================

namespace constexpr_formats {
/// Destination MAC address with explicit I/G and U/L bits (IEEE 802.3)
/// 48 bits: [I/G (1), U/L (1), rest (46)]
inline constexpr ConstexprPacketFormat<3> kDestMacWithExplicitBits{{{
    ConstexprFieldDef{"ig_bit", 1},  // Individual/Group bit (MSB)
    ConstexprFieldDef{"ul_bit", 1},  // Universal/Local bit
    ConstexprFieldDef{"mac_rest", 46}  // Remaining MAC bits
}}};

/// VLAN tag (802.1Q) - 16 bits
inline constexpr ConstexprPacketFormat<3> kVlanTag{{{
    ConstexprFieldDef{"pcp", 3},
    ConstexprFieldDef{"dei", 1},
    ConstexprFieldDef{"vid", 12},
}}};

/// Ethernet header (without VLAN) - 112 bits
inline constexpr ConstexprPacketFormat<3> kEthernetHeader{{{
    ConstexprFieldDef{"dest_mac", 48},
    ConstexprFieldDef{"src_mac", 48},
    ConstexprFieldDef{"ethertype", 16},
}}};

/// IPv4 header (fixed portion) - 160 bits
inline constexpr ConstexprPacketFormat<13> kIpv4Header{{{
    ConstexprFieldDef{"version", 4},
    ConstexprFieldDef{"ihl", 4},
    ConstexprFieldDef{"dscp", 6},
    ConstexprFieldDef{"ecn", 2},
    ConstexprFieldDef{"total_length", 16},
    ConstexprFieldDef{"identification", 16},
    ConstexprFieldDef{"flags", 3},
    ConstexprFieldDef{"fragment_offset", 13},
    ConstexprFieldDef{"ttl", 8},
    ConstexprFieldDef{"protocol", 8},
    ConstexprFieldDef{"header_checksum", 16},
    ConstexprFieldDef{"src_ip", 32},
    ConstexprFieldDef{"dst_ip", 32},
}}};

/// PCI Extended Capability Header - 32 bits
inline constexpr ConstexprPacketFormat<3> kPcieExtCapHeader{{{
    ConstexprFieldDef{"capability_id", 16},
    ConstexprFieldDef{"version", 4},
    ConstexprFieldDef{"next_offset", 12},
}}};

}  // namespace constexpr_formats

}  // namespace bit_fields
