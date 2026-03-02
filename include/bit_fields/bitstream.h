#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace bit_fields {

/// Endianness for wire format interpretation.
enum class WireOrder : std::uint8_t {
  BigEndian,  ///< Network byte order (MSB first)
  LittleEndian,  ///< LSB first
};

/// Compile-time field descriptor for packet formats.
struct FieldDef {
  std::string_view name;
  std::size_t bit_width;
};

/// Check if a field name represents padding/reserved bits.
/// Convention:
///   - Any field name starting with '_' (e.g. "_", "_reserved1", "_gap2") is treated as padding.
///   - Fields named exactly "reserved" or "padding" are also treated as padding.
///   - Use multiple fields with distinct names (e.g. "_pad1", "_pad2") for multiple gaps.
/// These fields are automatically skipped during parsing and serialization.
[[nodiscard]] constexpr bool is_padding_field(std::string_view name) noexcept {
  return !name.empty() && (name[0] == '_' || name == "reserved" || name == "padding");
}

/// Parsed field result: name + value pair.
struct ParsedField {
  std::string_view name;
  std::uint64_t value;
};

/// Compile-time packet format definition.
/// Usage: constexpr PacketFormat<3> kVlanHeader{{ {"pcp", 3}, {"dei", 1}, {"vid", 12} }};
template <std::size_t N>
struct PacketFormat {
  std::array<FieldDef, N> fields;

  /// Total bits in this format.
  [[nodiscard]] constexpr std::size_t total_bits() const noexcept {
    std::size_t sum = 0;
    for (const auto& f : fields) {
      sum += f.bit_width;
    }
    return sum;
  }

  /// Find field index by name (returns std::optional).
  [[nodiscard]] constexpr std::optional<std::size_t> field_index(std::string_view name) const {
    for (std::size_t i = 0; i < N; ++i) {
      if (fields[i].name == name) {
        return i;
      }
    }
    return std::nullopt;
  }

  /// Get the bit offset of a field by name (returns std::optional).
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

  /// Get the bit width of a field by name (returns std::optional).
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

/// Alias for register format (generic hardware registers, e.g., 64/128/256 bits)
template <std::size_t N>
using RegisterFormat = PacketFormat<N>;

/// Result of parsing a packet format: array of values indexed by field order.
template <std::size_t N>
struct ParsedPacket {
  const PacketFormat<N>* format;
  std::array<std::uint64_t, N> values;

  /// Get field value by index.
  [[nodiscard]] constexpr std::uint64_t operator[](std::size_t idx) const noexcept {
    return values[idx];
  }

  /// Get field value by name (runtime lookup).
  [[nodiscard]] std::uint64_t get(std::string_view name) const {
    auto idx_opt = format->field_index(name);
    if (!idx_opt || *idx_opt >= N) {
      throw std::out_of_range("ParsedPacket::get: unknown field name");
    }
    return values[*idx_opt];
  }

  /// Get field value by name with compile-time index (for known formats).
  template <std::size_t Idx>
  [[nodiscard]] constexpr std::uint64_t get() const noexcept {
    static_assert(Idx < N, "Field index out of range");
    return values[Idx];
  }
};

/// Expected field/value pair for verification helpers.
struct ExpectedField {
  std::string_view name;
  std::uint64_t value;
};

/// Compile-time or runtime table of expected field values.
template <std::size_t N>
using ExpectedTable = std::array<ExpectedField, N>;

/// Expected field check with a predicate (functor/lambda).
template <typename Predicate>
struct ExpectedCheck {
  std::string_view name;
  Predicate predicate;
};

template <typename Predicate>
ExpectedCheck(std::string_view, Predicate) -> ExpectedCheck<Predicate>;

/// Compile-time or runtime table of predicate checks.
template <typename Predicate, std::size_t N>
using ExpectedChecks = std::array<ExpectedCheck<Predicate>, N>;

/// A bit-level reader for parsing packed wire formats.
template <WireOrder Order = WireOrder::BigEndian>
class BitReader {
public:
  /// Get a span to a byte-aligned field's data in the buffer.
  /// Returns an empty span if the field is not found, not byte-aligned, or not a whole number of
  /// bytes.
  template <std::size_t N>
  [[nodiscard]] std::span<const std::byte> span_of_field(const PacketFormat<N>& format,
                                                         std::string_view name) const noexcept {
    auto bit_offset_opt = format.bit_offset_of(name);
    auto bit_width_opt  = format.field_width(name);
    if (!bit_offset_opt || !bit_width_opt)
      return {};
    std::size_t bit_offset = *bit_offset_opt;
    std::size_t bit_width  = *bit_width_opt;
    if (bit_offset % 8 != 0 || bit_width % 8 != 0)
      return {};
    std::size_t byte_offset = bit_offset / 8;
    std::size_t byte_len    = bit_width / 8;
    if (byte_offset + byte_len > buffer_.size())
      return {};
    return buffer_.subspan(byte_offset, byte_len);
  }
  explicit BitReader(std::span<const std::byte> buffer) noexcept
    : buffer_(buffer), bit_offset_(0) {}

  /// Construct from a reference to an integral type (e.g. uint32_t).
  /// The reader views the in-memory representation of the value.
  template <typename T>
    requires std::is_integral_v<T>
  explicit BitReader(const T& value) noexcept
    : buffer_(std::as_bytes(std::span<const T, 1>(&value, 1))), bit_offset_(0) {}

  /// Reset read position to the beginning.
  void reset() noexcept { bit_offset_ = 0; }

  /// Get current bit offset.
  [[nodiscard]] std::size_t bit_position() const noexcept { return bit_offset_; }

  /// Get current byte offset (rounded down).
  [[nodiscard]] std::size_t byte_position() const noexcept { return bit_offset_ / 8; }

  /// Check if there are at least N bits remaining.
  [[nodiscard]] bool has_bits(std::size_t n) const noexcept {
    return (bit_offset_ + n) <= (buffer_.size() * 8);
  }

  /// Read exactly N bits and return as an integer. Advances position.
  template <std::size_t N>
    requires(N > 0 && N <= 64)
  [[nodiscard]] std::uint64_t read_bits() {
    return read_bits_runtime(N);
  }

  /// Read N bits (runtime width). Advances position.
  [[nodiscard]] std::uint64_t read_bits_runtime(std::size_t n) {
    if (n == 0 || n > 64) {
      throw std::invalid_argument("BitReader::read_bits_runtime: width must be 1-64");
    }
    if (!has_bits(n)) {
      throw std::out_of_range("BitReader::read_bits_runtime: not enough bits remaining");
    }

    std::uint64_t value = 0;

    if constexpr (Order == WireOrder::BigEndian) {
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t byte_idx = bit_offset_ / 8;
        std::size_t bit_idx  = 7 - (bit_offset_ % 8);
        if (std::to_integer<int>(buffer_[byte_idx]) & (1 << bit_idx)) {
          value |= (1ULL << (n - 1 - i));
        }
        ++bit_offset_;
      }
    } else {
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t byte_idx = bit_offset_ / 8;
        std::size_t bit_idx  = bit_offset_ % 8;
        if (std::to_integer<int>(buffer_[byte_idx]) & (1 << bit_idx)) {
          value |= (1ULL << i);
        }
        ++bit_offset_;
      }
    }

    return value;
  }

  /// Read a single field from a packet format by name.
  /// Seeks to the field's position, reads it, and returns the value.
  /// More efficient than parse() when you only need one field.
  /// @throws std::out_of_range if field not found or not enough bits.
  template <std::size_t N>
  [[nodiscard]] std::uint64_t read_field(const PacketFormat<N>& format, std::string_view name) {
    auto offset_opt = format.bit_offset_of(name);
    auto width_opt  = format.field_width(name);

    if (!offset_opt || !width_opt || *width_opt == 0) {
      throw std::out_of_range("BitReader::read_field: unknown field name");
    }
    std::size_t offset = *offset_opt;
    std::size_t width  = *width_opt;
    if (!has_bits(offset + width)) {
      throw std::out_of_range("BitReader::read_field: not enough bits for field");
    }

    // Seek to field position (from current position 0)
    bit_offset_ = offset;
    return read_bits_runtime(width);
  }

  /// Deserialize an entire packet format, returning all field values.
  /// Fields named "reserved", "padding", or starting with '_' are skipped.
  template <std::size_t N>
  [[nodiscard]] ParsedPacket<N> deserialize(const PacketFormat<N>& format) {
    if (!has_bits(format.total_bits())) {
      throw std::out_of_range("BitReader::deserialize: not enough bits for format");
    }

    ParsedPacket<N> result{&format, {}};
    for (std::size_t i = 0; i < N; ++i) {
      if (is_padding_field(format.fields[i].name)) {
        skip_bits(format.fields[i].bit_width);
        result.values[i] = 0;  // Padding fields return 0
      } else {
        result.values[i] = read_bits_runtime(format.fields[i].bit_width);
      }
    }
    return result;
  }

  /// Verify parsed fields against exact expected values.
  template <std::size_t N, std::size_t M>
  [[nodiscard]] bool verify_expected(const ParsedPacket<N>& parsed,
                                     const ExpectedTable<M>& expected) const {
    for (const auto& entry : expected) {
      if (parsed.get(entry.name) != entry.value) {
        return false;
      }
    }
    return true;
  }

  /// Verify parsed fields using predicates (functors/lambdas).
  template <std::size_t N, typename Predicate, std::size_t M>
  [[nodiscard]] bool verify_expected(
      const ParsedPacket<N>& parsed,
      const std::array<ExpectedCheck<Predicate>, M>& expected) const {
    for (const auto& entry : expected) {
      if (!entry.predicate(parsed.get(entry.name))) {
        return false;
      }
    }
    return true;
  }

  /// Assert that parsed fields match expected values (debug only).
  template <std::size_t N, std::size_t M>
  void assert_expected([[maybe_unused]] const ParsedPacket<N>& parsed,
                       [[maybe_unused]] const ExpectedTable<M>& expected) const {
    assert(verify_expected(parsed, expected));
  }

  /// Assert that parsed fields pass predicate checks (debug only).
  template <std::size_t N, typename Predicate, std::size_t M>
  void assert_expected(
      [[maybe_unused]] const ParsedPacket<N>& parsed,
      [[maybe_unused]] const std::array<ExpectedCheck<Predicate>, M>& expected) const {
    assert(verify_expected(parsed, expected));
  }

  /// Deserialize a packet format into individual output references.
  /// Usage: reader.deserialize_into(format, pcp, dei, vid);
  /// Fields named "reserved", "padding", or starting with '_' are skipped (output set to 0).
  template <std::size_t N, typename... Args>
    requires(sizeof...(Args) == N)
  void deserialize_into(const PacketFormat<N>& format, Args&... outputs) {
    if (!has_bits(format.total_bits())) {
      throw std::out_of_range("BitReader::deserialize_into: not enough bits for format");
    }

    std::size_t idx = 0;
    (
        [&] {
          if (is_padding_field(format.fields[idx].name)) {
            skip_bits(format.fields[idx].bit_width);
            outputs = static_cast<std::remove_reference_t<Args>>(0);
          } else {
            outputs = static_cast<std::remove_reference_t<Args>>(
                read_bits_runtime(format.fields[idx].bit_width));
          }
          ++idx;
        }(),
        ...);
  }

  /// Read a byte-aligned unsigned integer.
  template <typename T>
    requires std::is_unsigned_v<T>
  [[nodiscard]] T read_aligned() {
    static_assert(sizeof(T) <= 8);
    constexpr std::size_t bits = sizeof(T) * 8;

    if (bit_offset_ % 8 != 0) {
      throw std::logic_error("BitReader::read_aligned: not byte-aligned");
    }
    if (!has_bits(bits)) {
      throw std::out_of_range("BitReader::read_aligned: not enough bytes remaining");
    }

    T value              = 0;
    std::size_t byte_idx = bit_offset_ / 8;

    if constexpr (Order == WireOrder::BigEndian) {
      for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = static_cast<T>((value << 8) | std::to_integer<std::uint8_t>(buffer_[byte_idx + i]));
      }
    } else {
      for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(std::to_integer<std::uint8_t>(buffer_[byte_idx + i]) << (i * 8));
      }
    }

    bit_offset_ += bits;
    return value;
  }

  /// Skip N bits without reading.
  void skip_bits(std::size_t n) {
    if (!has_bits(n)) {
      throw std::out_of_range("BitReader::skip_bits: not enough bits remaining");
    }
    bit_offset_ += n;
  }

  /// Align to the next byte boundary.
  void align_to_byte() noexcept {
    if (bit_offset_ % 8 != 0) {
      bit_offset_ = ((bit_offset_ / 8) + 1) * 8;
    }
  }

private:
  std::span<const std::byte> buffer_;
  std::size_t bit_offset_;
};

/// A bit-level writer for constructing packed wire formats.
template <WireOrder Order = WireOrder::BigEndian>
class BitWriter {
public:
  /// Get a span to a byte-aligned field's data in the buffer (for direct writing).
  /// Returns an empty span if the field is not found, not byte-aligned, or not a whole number of
  /// bytes.
  template <std::size_t N>
  [[nodiscard]] std::span<std::byte> span_of_field(const PacketFormat<N>& format,
                                                   std::string_view name) noexcept {
    auto bit_offset_opt = format.bit_offset_of(name);
    auto bit_width_opt  = format.field_width(name);
    if (!bit_offset_opt || !bit_width_opt)
      return {};
    std::size_t bit_offset = *bit_offset_opt;
    std::size_t bit_width  = *bit_width_opt;
    if (bit_offset % 8 != 0 || bit_width % 8 != 0)
      return {};
    std::size_t byte_offset = bit_offset / 8;
    std::size_t byte_len    = bit_width / 8;
    if (byte_offset + byte_len > buffer_.size())
      return {};
    return buffer_.subspan(byte_offset, byte_len);
  }
  explicit BitWriter(std::span<std::byte> buffer) noexcept : buffer_(buffer), bit_offset_(0) {
    for (auto& b : buffer_) {
      b = std::byte{0};
    }
  }

  /// Construct from a reference to an integral type (e.g. uint32_t).
  /// The writer operates on the in-memory representation of the value.
  template <typename T>
    requires std::is_integral_v<T>
  explicit BitWriter(T& value) noexcept
    : BitWriter(std::as_writable_bytes(std::span<T, 1>(&value, 1))) {}

  /// Get current bit offset.
  [[nodiscard]] std::size_t bit_position() const noexcept { return bit_offset_; }

  /// Get bytes written so far (rounded up).
  [[nodiscard]] std::size_t bytes_written() const noexcept { return (bit_offset_ + 7) / 8; }

  /// Check if there is space for N more bits.
  [[nodiscard]] bool has_space(std::size_t n) const noexcept {
    return (bit_offset_ + n) <= (buffer_.size() * 8);
  }

  /// Write exactly N bits from value.
  template <std::size_t N>
    requires(N > 0 && N <= 64)
  void write_bits(std::uint64_t value) {
    write_bits_runtime(N, value);
  }

  /// Write N bits (runtime width).
  void write_bits_runtime(std::size_t n, std::uint64_t value) {
    if (n == 0 || n > 64) {
      throw std::invalid_argument("BitWriter::write_bits_runtime: width must be 1-64");
    }
    if (!has_space(n)) {
      throw std::out_of_range("BitWriter::write_bits_runtime: not enough space remaining");
    }

    if constexpr (Order == WireOrder::BigEndian) {
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t byte_idx = bit_offset_ / 8;
        std::size_t bit_idx  = 7 - (bit_offset_ % 8);
        if (value & (1ULL << (n - 1 - i))) {
          buffer_[byte_idx] |= std::byte{static_cast<unsigned char>(1 << bit_idx)};
        }
        ++bit_offset_;
      }
    } else {
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t byte_idx = bit_offset_ / 8;
        std::size_t bit_idx  = bit_offset_ % 8;
        if (value & (1ULL << i)) {
          buffer_[byte_idx] |= std::byte{static_cast<unsigned char>(1 << bit_idx)};
        }
        ++bit_offset_;
      }
    }
  }

  /// Serialize values according to a packet format.
  /// Usage: writer.serialize(format, pcp_val, dei_val, vid_val);
  template <std::size_t N, typename... Args>
    requires(sizeof...(Args) == N)
  void serialize(const PacketFormat<N>& format, Args... values) {
    if (!has_space(format.total_bits())) {
      throw std::out_of_range("BitWriter::serialize: not enough space for format");
    }

    std::size_t idx = 0;
    (
        [&] {
          write_bits_runtime(format.fields[idx].bit_width, static_cast<std::uint64_t>(values));
          ++idx;
        }(),
        ...);
  }

  /// Write a byte-aligned unsigned integer.
  template <typename T>
    requires std::is_unsigned_v<T>
  void write_aligned(T value) {
    static_assert(sizeof(T) <= 8);
    constexpr std::size_t bits = sizeof(T) * 8;

    if (bit_offset_ % 8 != 0) {
      throw std::logic_error("BitWriter::write_aligned: not byte-aligned");
    }
    if (!has_space(bits)) {
      throw std::out_of_range("BitWriter::write_aligned: not enough space remaining");
    }

    std::size_t byte_idx = bit_offset_ / 8;

    if constexpr (Order == WireOrder::BigEndian) {
      for (std::size_t i = 0; i < sizeof(T); ++i) {
        buffer_[byte_idx + i] =
            std::byte{static_cast<unsigned char>((value >> ((sizeof(T) - 1 - i) * 8)) & 0xFF)};
      }
    } else {
      for (std::size_t i = 0; i < sizeof(T); ++i) {
        buffer_[byte_idx + i] = std::byte{static_cast<unsigned char>((value >> (i * 8)) & 0xFF)};
      }
    }

    bit_offset_ += bits;
  }

  /// Skip N bits (write zeros).
  void skip_bits(std::size_t n) {
    if (!has_space(n)) {
      throw std::out_of_range("BitWriter::skip_bits: not enough space remaining");
    }
    bit_offset_ += n;  // Buffer was zero-initialized
  }

  /// Pad with zeros to the next byte boundary.
  void align_to_byte() noexcept {
    if (bit_offset_ % 8 != 0) {
      bit_offset_ = ((bit_offset_ / 8) + 1) * 8;
    }
  }

private:
  std::span<std::byte> buffer_;
  std::size_t bit_offset_;
};

// Convenience aliases
using NetworkBitReader = BitReader<WireOrder::BigEndian>;
using NetworkBitWriter = BitWriter<WireOrder::BigEndian>;

}  // namespace bit_fields
