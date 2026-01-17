#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "bit_fields/bitstream.h"

using namespace bit_fields;

constexpr PacketFormat<3> kTestFormat{
    {std::to_array<FieldDef>({{"a", 8}, {"b", 8}, {"payload", 16}})}};

// Helper macro to test that an exception is thrown
#define EXPECT_THROW(expr, exception_type)                         \
  do {                                                             \
    bool caught = false;                                           \
    try {                                                          \
      (void) (expr);                                               \
    } catch (const exception_type&) {                              \
      caught = true;                                               \
    }                                                              \
    assert(caught && "Expected " #exception_type " to be thrown"); \
  } while (0)

void test_span_of_field_reader() {
  std::array<std::byte, 4> buf = {
      std::byte{0x01}, std::byte{0x02}, std::byte{0xAA}, std::byte{0xBB}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};
  auto span = reader.span_of_field(kTestFormat, "payload");
  assert(!span.empty());
  assert(span.size() == 2);
  assert(span[0] == std::byte{0xAA});
  assert(span[1] == std::byte{0xBB});
}

void test_span_of_field_writer() {
  std::array<std::byte, 4> buf = {
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
  NetworkBitWriter writer{std::span<std::byte>(buf)};
  auto span = writer.span_of_field(kTestFormat, "payload");
  assert(!span.empty());
  assert(span.size() == 2);
  span[0] = std::byte{0xCC};
  span[1] = std::byte{0xDD};
  // Check buffer updated
  assert(buf[2] == std::byte{0xCC});
  assert(buf[3] == std::byte{0xDD});
}

void test_span_of_field_alignment() {
  constexpr PacketFormat<2> kMisaligned{{std::to_array<FieldDef>({{"a", 4}, {"b", 12}})}};
  std::array<std::byte, 2> buf = {std::byte{0xF0}, std::byte{0x0F}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};
  auto span = reader.span_of_field(kMisaligned, "b");
  assert(span.empty());
}

void test_register_writer_reader_64() {
  using Register64 = std::array<std::byte, 8>;
  constexpr RegisterFormat<3> kReg64{
      {std::to_array<FieldDef>({{"low16", 16}, {"mid32", 32}, {"high16", 16}})}};

  Register64 buf{};
  BitWriter<WireOrder::BigEndian> writer{std::span<std::byte>(buf)};
  writer.serialize(kReg64, 0xBEEF, 0x89ABCDEF, 0x1234);

  // Verify serialized layout via reader and spans.
  BitReader<WireOrder::BigEndian> reader{std::span<const std::byte>(buf)};
  auto parsed = reader.deserialize(kReg64);
  assert(parsed.get("low16") == 0xBEEF);
  assert(parsed.get("mid32") == 0x89ABCDEF);
  assert(parsed.get("high16") == 0x1234);

  auto low_span  = reader.span_of_field(kReg64, "low16");
  auto mid_span  = reader.span_of_field(kReg64, "mid32");
  auto high_span = reader.span_of_field(kReg64, "high16");
  assert(low_span.size() == 2 && low_span[0] == std::byte{0xBE} && low_span[1] == std::byte{0xEF});
  assert(mid_span.size() == 4 && mid_span[0] == std::byte{0x89} && mid_span[3] == std::byte{0xEF});
  assert(high_span.size() == 2 && high_span[0] == std::byte{0x12}
         && high_span[1] == std::byte{0x34});
}

void test_register_little_endian() {
  using Register32 = std::array<std::byte, 4>;
  constexpr RegisterFormat<2> kRegLE{{std::to_array<FieldDef>({{"low16", 16}, {"high16", 16}})}};

  Register32 buf{};
  BitWriter<WireOrder::LittleEndian> writer{std::span<std::byte>(buf)};
  writer.serialize(kRegLE, 0x1122, 0x3344);

  // Little-endian serialize should place lower bits first in buffer.
  assert(buf[0] == std::byte{0x22});
  assert(buf[1] == std::byte{0x11});
  assert(buf[2] == std::byte{0x44});
  assert(buf[3] == std::byte{0x33});

  BitReader<WireOrder::LittleEndian> reader{std::span<const std::byte>(buf)};
  auto parsed = reader.deserialize(kRegLE);
  assert(parsed.get("low16") == 0x1122);
  assert(parsed.get("high16") == 0x3344);
}

void test_padding_skipped() {
  constexpr PacketFormat<4> kWithPadding{{std::to_array<FieldDef>({
      {"field_a", 4},
      {"_pad0", 4},  // padding
      {"field_b", 8},
      {"padding", 4},  // padding
  })}};
  std::array<std::byte, 3> buf = {std::byte{0xAB}, std::byte{0xCD}, std::byte{0x00}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};
  auto parsed = reader.deserialize(kWithPadding);
  assert(parsed.get("field_a") == 0xA);
  assert(parsed.get("field_b") == 0xCD);
  assert(parsed.values[1] == 0);  // padding skipped -> zero
  assert(parsed.values[3] == 0);
}

void test_register_span_misaligned() {
  // Span should be empty for non-byte-aligned register field.
  constexpr RegisterFormat<2> kRegMisaligned{{std::to_array<FieldDef>({{"f1", 12}, {"f2", 20}})}};
  std::array<std::byte, 4> buf = {
      std::byte{0xFF}, std::byte{0xEE}, std::byte{0xDD}, std::byte{0xCC}};
  BitReader<WireOrder::BigEndian> reader{std::span<const std::byte>(buf)};
  auto span = reader.span_of_field(kRegMisaligned, "f2");
  assert(span.empty());
}

void test_expected_field_checks() {
  constexpr PacketFormat<3> kExpectedFormat{
      {std::to_array<FieldDef>({{"a", 4}, {"b", 4}, {"c", 8}})}};
  std::array<std::byte, 2> buf{};
  NetworkBitWriter writer{std::span<std::byte>(buf)};
  writer.serialize(kExpectedFormat, 0xA, 0x5, 0xCC);

  NetworkBitReader reader{std::span<const std::byte>(buf)};
  auto parsed = reader.deserialize(kExpectedFormat);

  constexpr ExpectedTable<2> kExpected{{{"a", 0xA}, {"c", 0xCC}}};
  assert(reader.verify_expected(parsed, kExpected));
  reader.assert_expected(parsed, kExpected);

  constexpr ExpectedTable<1> kExpectedBad{{{"b", 0x6}}};
  assert(!reader.verify_expected(parsed, kExpectedBad));

  struct IsEven {
    bool operator()(std::uint64_t v) const { return (v % 2) == 0; }
  };
  constexpr std::array<ExpectedCheck<IsEven>, 1> kChecks{{{"a", IsEven{}}}};
  assert(reader.verify_expected(parsed, kChecks));
  reader.assert_expected(parsed, kChecks);
}

void test_expected_field_checks_register() {
  using Register64 = std::array<std::byte, 8>;
  constexpr RegisterFormat<3> kReg64{
      {std::to_array<FieldDef>({{"low16", 16}, {"mid32", 32}, {"high16", 16}})}};

  Register64 buf{};
  NetworkBitWriter writer{std::span<std::byte>(buf)};
  writer.serialize(kReg64, 0xBEEF, 0x89ABCDEF, 0x1234);

  NetworkBitReader reader{std::span<const std::byte>(buf)};
  auto parsed = reader.deserialize(kReg64);

  constexpr ExpectedTable<3> kRegExpected{
      {{"low16", 0xBEEF}, {"mid32", 0x89ABCDEF}, {"high16", 0x1234}}};
  assert(reader.verify_expected(parsed, kRegExpected));
  reader.assert_expected(parsed, kRegExpected);
}

void test_read_field() {
  std::array<std::byte, 4> buf = {
      std::byte{0xAB}, std::byte{0xCD}, std::byte{0x12}, std::byte{0x34}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  // Read individual fields by name
  auto val_a = reader.read_field(kTestFormat, "a");
  assert(val_a == 0xAB);

  reader.reset();
  auto val_b = reader.read_field(kTestFormat, "b");
  assert(val_b == 0xCD);

  reader.reset();
  auto val_payload = reader.read_field(kTestFormat, "payload");
  assert(val_payload == 0x1234);
}

void test_deserialize_into() {
  std::array<std::byte, 4> buf = {
      std::byte{0xAB}, std::byte{0xCD}, std::byte{0x12}, std::byte{0x34}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  std::uint64_t a, b, payload;
  reader.deserialize_into(kTestFormat, a, b, payload);

  assert(a == 0xAB);
  assert(b == 0xCD);
  assert(payload == 0x1234);
}

void test_deserialize_into_with_padding() {
  constexpr PacketFormat<4> kWithPadding{{std::to_array<FieldDef>({
      {"field_a", 4},
      {"_pad0", 4},
      {"field_b", 8},
      {"_pad1", 8},
  })}};
  std::array<std::byte, 3> buf = {std::byte{0xAB}, std::byte{0xCD}, std::byte{0xEF}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  std::uint64_t a, pad0, b, pad1;
  reader.deserialize_into(kWithPadding, a, pad0, b, pad1);

  assert(a == 0xA);
  assert(pad0 == 0);  // padding fields are set to 0
  assert(b == 0xCD);
  assert(pad1 == 0);  // padding fields are set to 0
}

void test_read_aligned() {
  // Big-endian aligned read
  std::array<std::byte, 8> buf = {std::byte{0x12},
                                  std::byte{0x34},
                                  std::byte{0x56},
                                  std::byte{0x78},
                                  std::byte{0x9A},
                                  std::byte{0xBC},
                                  std::byte{0xDE},
                                  std::byte{0xF0}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  auto val16 = reader.read_aligned<std::uint16_t>();
  assert(val16 == 0x1234);

  auto val32 = reader.read_aligned<std::uint32_t>();
  assert(val32 == 0x56789ABC);

  auto val8 = reader.read_aligned<std::uint8_t>();
  assert(val8 == 0xDE);
}

void test_read_aligned_little_endian() {
  std::array<std::byte, 4> buf = {
      std::byte{0x34}, std::byte{0x12}, std::byte{0x78}, std::byte{0x56}};
  BitReader<WireOrder::LittleEndian> reader{std::span<const std::byte>(buf)};

  auto val16 = reader.read_aligned<std::uint16_t>();
  assert(val16 == 0x1234);

  auto val16_2 = reader.read_aligned<std::uint16_t>();
  assert(val16_2 == 0x5678);
}

void test_write_aligned() {
  std::array<std::byte, 8> buf{};
  NetworkBitWriter writer{std::span<std::byte>(buf)};

  writer.write_aligned<std::uint16_t>(0x1234);
  writer.write_aligned<std::uint32_t>(0x56789ABC);
  writer.write_aligned<std::uint8_t>(0xDE);

  assert(buf[0] == std::byte{0x12});
  assert(buf[1] == std::byte{0x34});
  assert(buf[2] == std::byte{0x56});
  assert(buf[3] == std::byte{0x78});
  assert(buf[4] == std::byte{0x9A});
  assert(buf[5] == std::byte{0xBC});
  assert(buf[6] == std::byte{0xDE});
}

void test_write_aligned_little_endian() {
  std::array<std::byte, 4> buf{};
  BitWriter<WireOrder::LittleEndian> writer{std::span<std::byte>(buf)};

  writer.write_aligned<std::uint16_t>(0x1234);
  writer.write_aligned<std::uint16_t>(0x5678);

  assert(buf[0] == std::byte{0x34});
  assert(buf[1] == std::byte{0x12});
  assert(buf[2] == std::byte{0x78});
  assert(buf[3] == std::byte{0x56});
}

void test_position_methods() {
  std::array<std::byte, 4> buf = {
      std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  assert(reader.bit_position() == 0);
  assert(reader.byte_position() == 0);

  (void) reader.read_bits<8>();
  assert(reader.bit_position() == 8);
  assert(reader.byte_position() == 1);

  (void) reader.read_bits<4>();
  assert(reader.bit_position() == 12);
  assert(reader.byte_position() == 1);  // 12/8 = 1

  reader.reset();
  assert(reader.bit_position() == 0);
}

void test_writer_position_methods() {
  std::array<std::byte, 4> buf{};
  NetworkBitWriter writer{std::span<std::byte>(buf)};

  assert(writer.bit_position() == 0);
  assert(writer.bytes_written() == 0);

  writer.write_bits<8>(0xFF);
  assert(writer.bit_position() == 8);
  assert(writer.bytes_written() == 1);

  writer.write_bits<4>(0xA);
  assert(writer.bit_position() == 12);
  assert(writer.bytes_written() == 2);  // (12+7)/8 = 2
}

void test_align_to_byte_reader() {
  std::array<std::byte, 4> buf = {
      std::byte{0xFF}, std::byte{0xEE}, std::byte{0xDD}, std::byte{0xCC}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  (void) reader.read_bits<4>();  // Read 4 bits
  assert(reader.bit_position() == 4);

  reader.align_to_byte();
  assert(reader.bit_position() == 8);

  // Should be no-op when already aligned
  reader.align_to_byte();
  assert(reader.bit_position() == 8);
}

void test_align_to_byte_writer() {
  std::array<std::byte, 4> buf{};
  NetworkBitWriter writer{std::span<std::byte>(buf)};

  writer.write_bits<4>(0xA);
  assert(writer.bit_position() == 4);

  writer.align_to_byte();
  assert(writer.bit_position() == 8);

  // Should be no-op when already aligned
  writer.align_to_byte();
  assert(writer.bit_position() == 8);
}

void test_skip_bits_writer() {
  std::array<std::byte, 4> buf{};
  NetworkBitWriter writer{std::span<std::byte>(buf)};

  writer.write_bits<8>(0xFF);
  writer.skip_bits(8);  // Skip 8 bits (leave as zeros)
  writer.write_bits<8>(0xAA);

  assert(buf[0] == std::byte{0xFF});
  assert(buf[1] == std::byte{0x00});  // Skipped (zero-initialized)
  assert(buf[2] == std::byte{0xAA});
}

void test_has_bits_and_has_space() {
  std::array<std::byte, 2> buf{};
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  assert(reader.has_bits(16));
  assert(reader.has_bits(1));
  assert(!reader.has_bits(17));

  NetworkBitWriter writer{std::span<std::byte>(buf)};
  assert(writer.has_space(16));
  assert(writer.has_space(1));
  assert(!writer.has_space(17));
}

void test_error_paths() {
  std::array<std::byte, 2> buf{};

  // Reader: not enough bits
  NetworkBitReader reader{std::span<const std::byte>(buf)};
  EXPECT_THROW(reader.read_bits_runtime(32), std::out_of_range);

  // Reader: invalid width
  reader.reset();
  EXPECT_THROW(reader.read_bits_runtime(0), std::invalid_argument);
  EXPECT_THROW(reader.read_bits_runtime(65), std::invalid_argument);

  // Reader: skip beyond buffer
  reader.reset();
  EXPECT_THROW(reader.skip_bits(32), std::out_of_range);

  // Reader: deserialize not enough bits
  reader.reset();
  constexpr PacketFormat<1> kLargeFormat{{std::to_array<FieldDef>({{"big", 64}})}};
  EXPECT_THROW(reader.deserialize(kLargeFormat), std::out_of_range);

  // Reader: deserialize_into not enough bits
  reader.reset();
  std::uint64_t val;
  EXPECT_THROW(reader.deserialize_into(kLargeFormat, val), std::out_of_range);

  // Reader: read_field unknown field
  reader.reset();
  EXPECT_THROW(reader.read_field(kTestFormat, "nonexistent"), std::out_of_range);

  // Reader: read_field not enough bits
  reader.reset();
  EXPECT_THROW(reader.read_field(kTestFormat, "payload"), std::out_of_range);

  // Reader: read_aligned not byte-aligned
  reader.reset();
  (void) reader.read_bits<4>();
  EXPECT_THROW(reader.read_aligned<std::uint8_t>(), std::logic_error);

  // Reader: read_aligned not enough bytes
  reader.reset();
  EXPECT_THROW(reader.read_aligned<std::uint32_t>(), std::out_of_range);

  // Writer: not enough space
  NetworkBitWriter writer{std::span<std::byte>(buf)};
  EXPECT_THROW(writer.write_bits_runtime(32, 0), std::out_of_range);

  // Writer: invalid width
  EXPECT_THROW(writer.write_bits_runtime(0, 0), std::invalid_argument);
  EXPECT_THROW(writer.write_bits_runtime(65, 0), std::invalid_argument);

  // Writer: skip_bits beyond buffer
  EXPECT_THROW(writer.skip_bits(32), std::out_of_range);

  // Writer: serialize not enough space
  EXPECT_THROW(writer.serialize(kTestFormat, 1, 2, 3), std::out_of_range);

  // Writer: write_aligned not byte-aligned
  writer.write_bits<4>(0);
  EXPECT_THROW(writer.write_aligned<std::uint8_t>(0), std::logic_error);

  // Writer: write_aligned not enough space
  std::array<std::byte, 1> tiny_buf{};
  NetworkBitWriter tiny_writer{std::span<std::byte>(tiny_buf)};
  EXPECT_THROW(tiny_writer.write_aligned<std::uint32_t>(0), std::out_of_range);

  // ParsedPacket: get unknown field
  std::array<std::byte, 4> read_buf{};
  NetworkBitReader parse_reader{std::span<const std::byte>(read_buf)};
  auto parsed = parse_reader.deserialize(kTestFormat);
  EXPECT_THROW(parsed.get("nonexistent"), std::out_of_range);
}

void test_template_read_write_bits() {
  std::array<std::byte, 8> buf{};
  NetworkBitWriter writer{std::span<std::byte>(buf)};

  // Use template versions
  writer.write_bits<16>(0x1234);
  writer.write_bits<32>(0x56789ABC);
  writer.write_bits<8>(0xDE);

  NetworkBitReader reader{std::span<const std::byte>(buf)};
  assert(reader.read_bits<16>() == 0x1234);
  assert(reader.read_bits<32>() == 0x56789ABC);
  assert(reader.read_bits<8>() == 0xDE);
}

void test_parsed_packet_index_access() {
  std::array<std::byte, 4> buf = {
      std::byte{0xAB}, std::byte{0xCD}, std::byte{0x12}, std::byte{0x34}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};
  auto parsed = reader.deserialize(kTestFormat);

  // Test operator[] and get<>()
  assert(parsed[0] == 0xAB);
  assert(parsed[1] == 0xCD);
  assert(parsed[2] == 0x1234);

  assert(parsed.get<0>() == 0xAB);
  assert(parsed.get<1>() == 0xCD);
  assert(parsed.get<2>() == 0x1234);
}

void test_is_padding_field() {
  // Test various padding field patterns
  assert(is_padding_field("_"));
  assert(is_padding_field("_pad"));
  assert(is_padding_field("_reserved1"));
  assert(is_padding_field("reserved"));
  assert(is_padding_field("padding"));

  // Non-padding fields
  assert(!is_padding_field("field"));
  assert(!is_padding_field("myfield"));
  assert(!is_padding_field(""));  // empty is not padding
}

void test_span_of_field_not_found() {
  std::array<std::byte, 4> buf{};
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  auto span = reader.span_of_field(kTestFormat, "nonexistent");
  assert(span.empty());

  NetworkBitWriter writer{std::span<std::byte>(buf)};
  auto wspan = writer.span_of_field(kTestFormat, "nonexistent");
  assert(wspan.empty());
}

void test_little_endian_skip_bits() {
  // Test skip_bits with LittleEndian reader
  std::array<std::byte, 4> buf = {
      std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78}};
  BitReader<WireOrder::LittleEndian> reader{std::span<const std::byte>(buf)};

  reader.skip_bits(8);  // Skip first byte
  assert(reader.bit_position() == 8);

  auto val = reader.read_bits<8>();
  assert(val == 0x34);
}

void test_deserialize_into_little_endian() {
  // Test deserialize_into with LittleEndian reader
  constexpr PacketFormat<2> kLE16{{std::to_array<FieldDef>({{"low8", 8}, {"high8", 8}})}};
  std::array<std::byte, 2> buf = {std::byte{0x12}, std::byte{0x34}};
  BitReader<WireOrder::LittleEndian> reader{std::span<const std::byte>(buf)};

  std::uint64_t low, high;
  reader.deserialize_into(kLE16, low, high);

  assert(low == 0x12);
  assert(high == 0x34);
}

void test_deserialize_into_single_field() {
  // Test deserialize_into with a single field (BigEndian)
  constexpr PacketFormat<1> kSingle{{std::to_array<FieldDef>({{"value", 16}})}};
  std::array<std::byte, 2> buf = {std::byte{0xAB}, std::byte{0xCD}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  std::uint64_t value;
  reader.deserialize_into(kSingle, value);

  assert(value == 0xABCD);
}

void test_verify_expected_predicate_fails() {
  // Test verify_expected with a predicate that returns false
  constexpr PacketFormat<2> kFormat{{std::to_array<FieldDef>({{"a", 8}, {"b", 8}})}};
  std::array<std::byte, 2> buf = {std::byte{0x05}, std::byte{0x10}};
  NetworkBitReader reader{std::span<const std::byte>(buf)};
  auto parsed = reader.deserialize(kFormat);

  // Predicate that checks if value is greater than 100
  struct GreaterThan100 {
    bool operator()(std::uint64_t v) const { return v > 100; }
  };

  // 'a' is 5, which is NOT > 100, so predicate should fail
  constexpr std::array<ExpectedCheck<GreaterThan100>, 1> kChecks{{{"a", GreaterThan100{}}}};
  assert(!reader.verify_expected(parsed, kChecks));
}

void test_writer_span_misaligned() {
  // Test BitWriter span_of_field when field is not byte-aligned
  constexpr PacketFormat<2> kMisaligned{{std::to_array<FieldDef>({{"a", 4}, {"b", 12}})}};
  std::array<std::byte, 2> buf{};
  NetworkBitWriter writer{std::span<std::byte>(buf)};

  // 'b' starts at bit 4, not byte-aligned
  auto span = writer.span_of_field(kMisaligned, "b");
  assert(span.empty());
}

void test_writer_span_out_of_bounds() {
  // Test BitWriter span_of_field when field extends beyond buffer
  constexpr PacketFormat<2> kOob{{std::to_array<FieldDef>({{"a", 8}, {"b", 32}})}};
  std::array<std::byte, 4> buf{};  // Only 4 bytes, but 'b' needs bytes 1-4 (total 5 bytes needed)
  NetworkBitWriter writer{std::span<std::byte>(buf)};

  auto span = writer.span_of_field(kOob, "b");
  assert(span.empty());
}

void test_reader_span_out_of_bounds() {
  // Test BitReader span_of_field when field extends beyond buffer
  constexpr PacketFormat<2> kOob{{std::to_array<FieldDef>({{"a", 8}, {"b", 32}})}};
  std::array<std::byte, 4> buf{};  // Only 4 bytes, but 'b' needs bytes 1-4 (total 5 bytes needed)
  NetworkBitReader reader{std::span<const std::byte>(buf)};

  auto span = reader.span_of_field(kOob, "b");
  assert(span.empty());
}

int main() {
  auto run = [](const char* name, auto fn) {
    fn();
    std::cout << "[PASS] " << name << "\n";
  };

  run("span_of_field_reader", test_span_of_field_reader);
  run("span_of_field_writer", test_span_of_field_writer);
  run("span_of_field_alignment", test_span_of_field_alignment);
  run("register_writer_reader_64", test_register_writer_reader_64);
  run("register_little_endian", test_register_little_endian);
  run("padding_skipped", test_padding_skipped);
  run("register_span_misaligned", test_register_span_misaligned);
  run("expected_field_checks", test_expected_field_checks);
  run("expected_field_checks_register", test_expected_field_checks_register);
  run("read_field", test_read_field);
  run("deserialize_into", test_deserialize_into);
  run("deserialize_into_with_padding", test_deserialize_into_with_padding);
  run("read_aligned", test_read_aligned);
  run("read_aligned_little_endian", test_read_aligned_little_endian);
  run("write_aligned", test_write_aligned);
  run("write_aligned_little_endian", test_write_aligned_little_endian);
  run("position_methods", test_position_methods);
  run("writer_position_methods", test_writer_position_methods);
  run("align_to_byte_reader", test_align_to_byte_reader);
  run("align_to_byte_writer", test_align_to_byte_writer);
  run("skip_bits_writer", test_skip_bits_writer);
  run("has_bits_and_has_space", test_has_bits_and_has_space);
  run("error_paths", test_error_paths);
  run("template_read_write_bits", test_template_read_write_bits);
  run("parsed_packet_index_access", test_parsed_packet_index_access);
  run("is_padding_field", test_is_padding_field);
  run("span_of_field_not_found", test_span_of_field_not_found);
  run("little_endian_skip_bits", test_little_endian_skip_bits);
  run("deserialize_into_little_endian", test_deserialize_into_little_endian);
  run("deserialize_into_single_field", test_deserialize_into_single_field);
  run("verify_expected_predicate_fails", test_verify_expected_predicate_fails);
  run("writer_span_misaligned", test_writer_span_misaligned);
  run("writer_span_out_of_bounds", test_writer_span_out_of_bounds);
  run("reader_span_out_of_bounds", test_reader_span_out_of_bounds);

  std::cout << "All tests passed.\n";
  return 0;
}
