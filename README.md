# bit_fields

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![Header-only](https://img.shields.io/badge/header--only-yes-brightgreen)

A C++20 header-only library for declarative bit-level packet and register parsing, serialization, and deserialization.

## Features

- **Metadata-driven parsing**: Define packet or register formats once, parse/serialize automatically
- **Endianness-aware**: Templates for big-endian (network) and little-endian (PCIe) wire formats
- **Arbitrary bit widths**: Read/write 1-64 bit fields at any bit offset
- **Zero-copy**: Works on `std::span`, no internal allocation
- **Type-safe**: Compile-time checked field counts, bounds checking at runtime
- **Verification helpers**: `verify_expected(...)` and `assert_expected(...)` for field validation
- **Pre-defined formats**: Common network protocols (Ethernet, IPv4, TCP, UDP, VLAN, etc.)

## Requirements

- C++20 compiler (GCC 10+, Clang 11+, MSVC 19.29+)
- Header-only, no dependencies

## Installation

### CMake FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
  bit_fields
  GIT_REPOSITORY https://github.com/rosslwheeler/bit_fields.git
  GIT_TAG main
)

FetchContent_MakeAvailable(bit_fields)

target_link_libraries(your_target PRIVATE bit_fields::bit_fields)
```

### Copy headers

Copy `include/bit_fields` into your project's include path and then:

```cpp
#include "bit_fields/bit_fields.h"
```

## Quick Start

```cpp
#include "bit_fields/bit_fields.h"
using namespace bit_fields;

// Define a custom packet format
constexpr PacketFormat<3> kMyHeader{{
    {"field_a", 4},
    {"field_b", 12},
    {"field_c", 16},
}};

// Deserialize from a buffer
void deserialize(std::span<const std::byte> data) {
    NetworkBitReader reader(data);
    auto packet = reader.deserialize(kMyHeader);
    
    // Access by name
    auto a = packet.get("field_a");
    
    // Access by index
    auto b = packet[1];
}

// Serialize to a buffer
void serialize(std::span<std::byte> buffer) {
    NetworkBitWriter writer(buffer);
    writer.serialize(kMyHeader, 0xA, 0x123, 0x4567);
}
```

## Example: Extract I/G and U/L bits from a destination MAC address

```cpp
#include "bit_fields/bit_fields.h"
using namespace bit_fields;

// Pre-defined format for destination MAC with explicit bits
constexpr PacketFormat<3> kDestMacWithExplicitBits{{{
    {"ig_bit", 1},   // Individual/Group bit (MSB)
    {"ul_bit", 1},   // Universal/Local bit
    {"mac_rest", 46} // Remaining MAC bits
}};

void parse_mac(std::span<const std::byte> data) {
    NetworkBitReader reader(data);
    auto mac = reader.deserialize(kDestMacWithExplicitBits);
    auto ig = mac.get("ig_bit");   // 0 = Individual, 1 = Group
    auto ul = mac.get("ul_bit");   // 0 = Universal, 1 = Local
    auto rest = mac.get("mac_rest"); // Lower 46 bits of MAC
}
```

## API Overview

### Accessing Raw Field Data (Pointer/Span)

For fields that are byte-aligned and a whole number of bytes, you can get a direct span to the field's data in the buffer:

```cpp
// For reading (const):
auto payload = reader.span_of_field(format, "payload"); // std::span<const std::byte>

// For writing (mutable):
auto payload = writer.span_of_field(format, "payload"); // std::span<std::byte>

// Example: get a pointer to a large payload field
if (!payload.empty()) {
    // payload.data() is a pointer to the start of the field
    // payload.size() is the length in bytes
}
```

**Caveats:**
- The field must be byte-aligned (bit offset and width are multiples of 8).
- Returns an empty span if the field is not found, not aligned, or out of bounds.
- The returned pointer/span is only valid as long as the underlying buffer is valid.

### PacketFormat<N>

Compile-time packet format definition:

```cpp
constexpr PacketFormat<3> kVlanTag{{
    {"pcp", 3},
    {"dei", 1},
    {"vid", 12},
}};
```

### BitReader<WireOrder>

Bit-level reader with automatic position tracking:

```cpp
NetworkBitReader reader(buffer);  // Big-endian (network order)

// Deserialize entire format
auto result = reader.deserialize(kVlanTag);

// Deserialize into variables
std::uint8_t pcp, dei;
std::uint16_t vid;
reader.deserialize_into(kVlanTag, pcp, dei, vid);

// Read a single field by name (efficient - skips to field position)
auto flags = reader.read_field(formats::kIpv4Header, "flags");
auto ttl = reader.read_field(formats::kIpv4Header, "ttl");

// Manual reading
auto bits = reader.read_bits<12>();
auto aligned = reader.read_aligned<std::uint32_t>();
reader.skip_bits(8);
reader.align_to_byte();
```

### Expected Field Validation

Use `verify_expected(...)` (returns `bool`) or `assert_expected(...)` (debug-only) to validate
parsed packets or registers against expected values or predicate checks.

```cpp
constexpr ExpectedTable<5> kIpv4Expected{{
    {"version", 4},
    {"ihl", 5},
    {"total_length", 120},
    {"ttl", 64},
    {"protocol", 6},
}};

struct TtlInRange {
  bool operator()(std::uint64_t v) const { return v >= 1 && v <= 64; }
};

constexpr ExpectedChecks<TtlInRange, 1> kIpv4Checks{{
    {"ttl", TtlInRange{}},
}};

auto ipv4 = reader.deserialize(formats::kIpv4Header);
bool ok = reader.verify_expected(ipv4, kIpv4Expected);
reader.assert_expected(ipv4, kIpv4Checks);
```

Runtime values can use the same table type:

```cpp
std::array<ExpectedField, 2> kIpv4Runtime = {{
    {"src_ip", ipv4_cfg.src_ip},
    {"dst_ip", ipv4_cfg.dst_ip},
}};
bool ips_ok = reader.verify_expected(ipv4, kIpv4Runtime);
```

These helpers work for registers too because register parsing returns the same `ParsedPacket<N>`
type:

```cpp
constexpr ExpectedTable<3> kRegExpected{{
    {"low16", 0xBEEF},
    {"mid32", 0x89ABCDEF},
    {"high16", 0x1234},
}};

auto reg = reader.deserialize(kReg64);
reader.assert_expected(reg, kRegExpected);
```

### PacketFormat Utilities

Query field positions without parsing:

```cpp
// Get field metadata (all return std::optional)
auto offset = formats::kIpv4Header.bit_offset_of("flags");  // std::optional<std::size_t>
auto width = formats::kIpv4Header.field_width("flags");     // std::optional<std::size_t>
auto index = formats::kIpv4Header.field_index("flags");     // std::optional<std::size_t>

// Check for not-found
if (offset) {
    // Use *offset
} else {
    // Field not found
}

// Or use value_or() for a default
std::size_t idx = formats::kIpv4Header.field_index("flags").value_or(0);
```

All three helpers return `std::optional<std::size_t>`. If the field is not found, they return `std::nullopt`.

### Register Formats

`RegisterFormat<N>` is an alias for `PacketFormat<N>` when describing hardware registers (e.g., 64/128/256-bit and greater total width). Individual fields are 1–64 bits. You can reuse the same reader/writer APIs:

```cpp
using namespace bit_fields;

// 64-bit register split into low16/mid32/high16
constexpr RegisterFormat<3> kReg64{{
    {"low16", 16},
    {"mid32", 32},
    {"high16", 16},
}};

std::array<std::byte, 8> reg{};

// Write fields (big-endian register here)
BitWriter<WireOrder::BigEndian> writer{std::span<std::byte>(reg)};
writer.serialize(kReg64, 0xBEEF, 0x89ABCDEF, 0x1234);

// Read fields back
BitReader<WireOrder::BigEndian> reader{std::span<const std::byte>(reg)};
auto parsed = reader.deserialize(kReg64);
auto low    = parsed.get("low16");   // 0xBEEF
auto mid    = parsed.get("mid32");   // 0x89ABCDEF
auto high   = parsed.get("high16");  // 0x1234

// Direct span access to aligned register fields
auto mid_span = reader.span_of_field(kReg64, "mid32"); // std::span<const std::byte>
```

Use `BitWriter<WireOrder::LittleEndian>` / `BitReader<WireOrder::LittleEndian>` for PCIe/MMIO style little-endian registers. `span_of_field` works for byte-aligned register fields to allow zero-copy access.

### BitWriter<WireOrder>

Bit-level writer:

```cpp
NetworkBitWriter writer(buffer);

// Serialize format
writer.serialize(kVlanTag, pcp, dei, vid);

// Manual writing
writer.write_bits<12>(0x123);
writer.write_aligned<std::uint32_t>(0xDEADBEEF);
writer.skip_bits(8);       // Skip 8 bits (writes zeros)
writer.align_to_byte();
```

### Handling Gaps and Padding

Reserved and padding fields are **automatically skipped** during parsing. The library recognizes these naming conventions:

- **Field names starting with `_`** (e.g., `"_reserved"`, `"_gap"`, `"_pad1"`, `"_pad2"`). Use distinct names for multiple gaps.
- **Field names equal to `"reserved"` or `"padding"`** (for single gaps).

```cpp
// Multiple gaps/padding fields:
constexpr PacketFormat<7> kMyFormat{{
    {"version", 4},
    {"_reserved1", 4},    // Auto-skipped (underscore prefix)
    {"flags", 8},
    {"_gap1", 2},         // Auto-skipped (underscore prefix)
    {"reserved", 12},     // Auto-skipped (named "reserved")
    {"_pad2", 6},         // Auto-skipped (underscore prefix)
    {"data", 32},
}};

// All fields with names starting with '_' (any suffix) are treated as padding and skipped.
// Use unique names for clarity if you have multiple gaps.

// Deserialize - reserved fields are skipped, return 0
auto packet = reader.deserialize(kMyFormat);
auto version = packet.get("version");  // Actual value
auto flags = packet.get("flags");      // Actual value
auto reserved = packet.get("reserved"); // Always 0
```

#### Manual Skip Methods

For finer control, you can also skip bits manually:

```cpp
NetworkBitReader reader(buffer);

auto field1 = reader.read_bits<8>();
reader.skip_bits(12);    // Skip 12 bits explicitly
auto field2 = reader.read_bits<16>();
reader.align_to_byte();  // Skip to next byte boundary
```

### Pre-defined Formats

Located in `bit_fields/formats.h`:

| Format | Description | Bits |
|--------|-------------|------|
| `kEthernetHeader` | Ethernet II header | 112 |
| `kVlanTag` | 802.1Q VLAN tag | 16 |
| `kIpv4Header` | IPv4 header (fixed) | 160 |
| `kTcpHeader` | TCP header (fixed) | 160 |
| `kUdpHeader` | UDP header | 64 |
| `kArpHeader` | ARP header | 224 |
| `kIcmpHeader` | ICMP header | 64 |
| `kPcieExtCapHeader` | PCIe Extended Capability | 32 |
| `kPciCapHeader` | PCI Standard Capability | 16 |
| `kPciMemoryBar32` | PCI Memory BAR | 32 |
| `kPciIoBar` | PCI I/O BAR | 32 |

## Endianness

The library supports two wire orders:

- **`WireOrder::BigEndian`** (default): Network byte order, MSB first
- **`WireOrder::LittleEndian`**: LSB first (for PCIe, x86 memory-mapped registers)

```cpp
// Network protocols (big-endian)
NetworkBitReader reader(data);  // alias for BitReader<WireOrder::BigEndian>

// PCIe/memory-mapped (little-endian)
BitReader<WireOrder::LittleEndian> pcie_reader(data);
```

## Compile-Time vs Runtime Parsing

The library provides two variants:

| Variant | Header | Buffer Type | Error Handling | Use Case |
|---------|--------|-------------|----------------|----------|
| **Runtime** | `bitstream.h` | `std::span` | Exceptions | Dynamic packets from network/files |
| **Constexpr** | `constexpr_bitstream.h` | `std::array` | `std::optional`/`bool` | Static constants, tests, lookup tables |

### When to Use Constexpr

Best for **static/constant packet definitions** known at compile time:

| Use Case | Example |
|----------|---------|
| Protocol constants | Magic bytes, headers, version identifiers |
| Test vectors | Known-good packets for unit tests |
| Lookup tables | Pre-parsed packet templates |
| Configuration | Static capability structures, register defaults |
| Validation | Verify packet format definitions are correct |

```cpp
#include "bit_fields/constexpr_bitstream.h"

// Static test vector - parsed at compile time, zero runtime cost
constexpr std::array<std::byte, 2> kVlanData = {std::byte{0xA0}, std::byte{0x64}};
constexpr auto kParsed = make_constexpr_reader(kVlanData).deserialize(constexpr_formats::kVlanTag);

static_assert(kParsed[0] == 5);   // PCP verified at compile time!
static_assert(kParsed[2] == 100); // VID verified at compile time!

// Pre-built capability header for device initialization
constexpr auto build_msi_cap() {
    ConstexprBitWriter<constexpr_formats::kPcieExtCapHeader.total_bytes()> w;
    w.serialize(constexpr_formats::kPcieExtCapHeader, 0x0005, 1, 0x000);
    return w.data();
}
constexpr auto kMsiCapHeader = build_msi_cap();  // Zero runtime cost
```

### When to Use Runtime

Use for **dynamic data** not known until runtime:

| Scenario | Why Runtime |
|----------|-------------|
| Incoming network packets | Data not known until runtime |
| File parsing | Reading from disk/stream |
| User input | Dynamic configuration |
| Large buffers | `std::span` avoids copying |
| Variable-length packets | Size determined at runtime |

```cpp
#include "bit_fields/bit_fields.h"

void handle_packet(std::span<const std::byte> packet) {
    NetworkBitReader reader(packet);  // Runtime version
    auto eth = reader.deserialize(formats::kEthernetHeader);
    // Process dynamic data...
}
```

### Hybrid Approach

Use **constexpr formats** with the **runtime reader** — format metadata is compile-time, only deserializing is runtime:

```cpp
#include "bit_fields/bit_fields.h"
using namespace bit_fields;

// Deserializing happens at runtime with dynamic data,
// but the format definition (formats::kVlanTag) is constexpr
void process(std::span<const std::byte> data) {
    NetworkBitReader reader(data);
    auto vlan = reader.deserialize(formats::kVlanTag);  // Runtime deserialize, constexpr format
}
```

### Fast Comparison with Constexpr Data

Build expected packet data at compile time, then use `memcmp` for **10-20x faster** comparisons than field-by-field checking:

```cpp
#include "bit_fields/constexpr_bitstream.h"
#include <cstring>

// Build expected header at compile time
constexpr auto build_expected_vlan() {
    ConstexprBitWriter<2> w;
    w.serialize(constexpr_formats::kVlanTag, 5, 0, 100);  // PCP=5, DEI=0, VID=100
    return w.data();
}
constexpr auto kExpectedVlan = build_expected_vlan();  // Computed at compile time!

// Fast runtime comparison - single memcmp vs multiple field checks
bool is_expected_vlan(std::span<const std::byte> packet) {
    if (packet.size() < kExpectedVlan.size()) return false;
    
    // ~10-20x faster than deserializing and comparing each field
    return std::memcmp(packet.data(), kExpectedVlan.data(), kExpectedVlan.size()) == 0;
}

// Compare to the slow way:
bool is_expected_vlan_slow(std::span<const std::byte> packet) {
    NetworkBitReader reader(packet);
    auto vlan = reader.deserialize(formats::kVlanTag);
    return vlan.get("pcp") == 5 && vlan.get("dei") == 0 && vlan.get("vid") == 100;
}
```

This pattern is ideal for:
- **Magic number checks** - verify packet signatures
- **Protocol detection** - match known header patterns
- **Filter rules** - fast packet classification
- **Unit tests** - compare against known-good data

## Error Handling

All runtime operations throw exceptions on error:

- `std::out_of_range`: Not enough bits/space remaining
- `std::invalid_argument`: Invalid bit width (0 or >64)
- `std::logic_error`: Alignment violation for `read_aligned`/`write_aligned`

Constexpr operations return `std::optional` or `bool` instead of throwing.

### Bounds Checking

The reader/writer always know how much data is available (via `std::span`'s embedded size), and check before every read or write:

```cpp
// std::span = pointer + size bundled together
std::array<std::byte, 2> vlan_buffer{};
std::span<std::byte> buffer_view(vlan_buffer);  // knows it's 2 bytes (16 bits)

// Writer knows it has 16 bits of space
NetworkBitWriter writer(buffer_view);
writer.serialize(formats::kVlanTag, 5, 0, 100);  // OK: VLAN tag is 16 bits

// Reader knows it has 16 bits of data
NetworkBitReader reader(buffer_view);
auto parsed = reader.deserialize(formats::kVlanTag);  // OK: 16 bits
auto pcp = parsed.get("pcp");  // 5
auto vid = parsed.get("vid");  // 100

// This throws — IPv4 header needs 160 bits, span only has 16
reader.reset();
auto ip = reader.deserialize(formats::kIpv4Header);  // throws std::out_of_range!
```

You can also check manually before reading:

```cpp
if (reader.has_bits(160)) {
    auto ip = reader.deserialize(formats::kIpv4Header);
} else {
    // Handle truncated packet
}
```

This prevents buffer overruns — you'll never accidentally read past the end of your data.

## License

MIT License - see LICENSE file.
