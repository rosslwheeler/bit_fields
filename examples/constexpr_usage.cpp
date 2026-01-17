/// @file constexpr_usage.cpp
/// @brief Compile-time usage examples for the bit_fields library.
///
/// All parsing and serialization in this file happens at compile time.
/// The static_assert statements verify correctness with zero runtime cost.

#include <iostream>

#include "bit_fields/constexpr_bitstream.h"

using namespace bit_fields;
using namespace bit_fields::constexpr_formats;

// ============================================================================
// Example 1: Compile-time VLAN tag deserializing
// ============================================================================

// Input data: PCP=5, DEI=0, VID=100
// Binary: 101 0 0000 0110 0100 = 0xA064
constexpr std::array<std::byte, 2> VlanData = {std::byte{0xA0}, std::byte{0x64}};

// Deserialize at compile time
constexpr auto deserialize_vlan_compile_time() {
  auto reader = make_constexpr_reader(VlanData);
  return reader.deserialize(kVlanTag);
}

// All of this is computed at compile time!
constexpr auto ParsedVlan = deserialize_vlan_compile_time();

// Verify at compile time with static_assert
static_assert(ParsedVlan.valid, "VLAN parsing must succeed");
static_assert(ParsedVlan[0] == 5, "PCP must be 5");
static_assert(ParsedVlan[1] == 0, "DEI must be 0");
static_assert(ParsedVlan[2] == 100, "VID must be 100");

// ============================================================================
// Example 2: Compile-time VLAN tag serialization
// ============================================================================

constexpr auto build_vlan_compile_time() {
  ConstexprBitWriter<2> writer;
  writer.serialize(kVlanTag, 5, 0, 100);  // PCP=5, DEI=0, VID=100
  return writer.data();
}

constexpr auto BuiltVlan = build_vlan_compile_time();

// Verify round-trip at compile time!
static_assert(BuiltVlan == VlanData, "Round-trip must produce identical bytes");

// ============================================================================
// Example 3: Compile-time IPv4 header deserializing
// ============================================================================

// Sample IPv4 header (20 bytes):
// Version=4, IHL=5, DSCP=0, ECN=0, TotalLen=40, ID=0x1234
// Flags=0, FragOffset=0, TTL=64, Protocol=6 (TCP), Checksum=0xABCD
// SrcIP=192.168.1.1, DstIP=10.0.0.1
constexpr std::array<std::byte, 20> Ipv4Data = {
    std::byte{0x45},  // Version=4, IHL=5
    std::byte{0x00},  // DSCP=0, ECN=0
    std::byte{0x00}, std::byte{0x28},  // Total length = 40
    std::byte{0x12}, std::byte{0x34},  // Identification = 0x1234
    std::byte{0x00}, std::byte{0x00},  // Flags=0, Fragment offset=0
    std::byte{0x40},  // TTL = 64
    std::byte{0x06},  // Protocol = 6 (TCP)
    std::byte{0xAB}, std::byte{0xCD},  // Header checksum = 0xABCD
    std::byte{0xC0}, std::byte{0xA8}, std::byte{0x01}, std::byte{0x01},  // Src IP = 192.168.1.1
    std::byte{0x0A}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},  // Dst IP = 10.0.0.1
};

constexpr auto deserialize_ipv4_compile_time() {
  auto reader = make_constexpr_reader(Ipv4Data);
  return reader.deserialize(kIpv4Header);
}

constexpr auto ParsedIpv4 = deserialize_ipv4_compile_time();

static_assert(ParsedIpv4.valid, "IPv4 parsing must succeed");
static_assert(ParsedIpv4[0] == 4, "Version must be 4");
static_assert(ParsedIpv4[1] == 5, "IHL must be 5");
static_assert(ParsedIpv4[6] == 0, "Flags must be 0");
static_assert(ParsedIpv4[8] == 64, "TTL must be 64");
static_assert(ParsedIpv4[9] == 6, "Protocol must be TCP (6)");
static_assert(ParsedIpv4[11] == 0xC0A80101, "Src IP must be 192.168.1.1");
static_assert(ParsedIpv4[12] == 0x0A000001, "Dst IP must be 10.0.0.1");

// ============================================================================
// Example 4: Compile-time PCIe Extended Capability Header (little-endian)
// ============================================================================

// PCIe is little-endian on the wire
// Capability ID = 0x0010 (AER), Version = 2, Next = 0x100
// Little-endian layout: bytes are reversed
constexpr std::array<std::byte, 4> PcieCapData = {
    std::byte{0x10},  // Capability ID low byte
    std::byte{0x00},  // Capability ID high byte
    std::byte{0x02},  // Version (4 bits) + Next low (4 bits)
    std::byte{0x10},  // Next high (8 bits)
};

constexpr auto deserialize_pcie_cap_compile_time() {
  auto reader = make_constexpr_reader_le(PcieCapData);
  return reader.deserialize(kPcieExtCapHeader);
}

constexpr auto ParsedPcieCap = deserialize_pcie_cap_compile_time();

static_assert(ParsedPcieCap.valid, "PCIe cap parsing must succeed");
static_assert(ParsedPcieCap[0] == 0x0010, "Capability ID must be 0x0010 (AER)");
static_assert(ParsedPcieCap[1] == 2, "Version must be 2");
static_assert(ParsedPcieCap[2] == 0x100, "Next offset must be 0x100");

// ============================================================================
// Example 5: Custom format definition at compile time
// ============================================================================

// Define a custom 24-bit format
constexpr ConstexprPacketFormat<4> CustomFormat{std::array<bit_fields::ConstexprFieldDef, 4>{
    {{"field_a", 4}, {"field_b", 8}, {"field_c", 4}, {"field_d", 8}}}};

static_assert(CustomFormat.total_bits() == 24);
static_assert(CustomFormat.total_bytes() == 3);
static_assert(CustomFormat.field_index("field_c") == 2);
static_assert(!CustomFormat.field_index("nonexistent").has_value());  // Missing -> nullopt

constexpr std::array<std::byte, 3> CustomData = {
    std::byte{0xAB},  // field_a=0xA, field_b high 4 bits=0xB
    std::byte{0xCD},  // field_b low 4 bits=0xC, field_c=0xD... wait, let me recalc
    std::byte{0xEF},
};

// Actually for big-endian:
// Byte 0: field_a (4 bits) = 0xA, field_b high 4 bits = 0xB
// Byte 1: field_b low 4 bits = 0xC, field_c (4 bits) = 0xD
// Byte 2: field_d (8 bits) = 0xEF
// So: field_a=0xA, field_b=0xBC, field_c=0xD, field_d=0xEF

constexpr auto deserialize_custom_compile_time() {
  auto reader = make_constexpr_reader(CustomData);
  return reader.deserialize(CustomFormat);
}

constexpr auto ParsedCustom = deserialize_custom_compile_time();

static_assert(ParsedCustom.valid);
static_assert(ParsedCustom[0] == 0xA, "field_a");
static_assert(ParsedCustom[1] == 0xBC, "field_b");
static_assert(ParsedCustom[2] == 0xD, "field_c");
static_assert(ParsedCustom[3] == 0xEF, "field_d");

// ============================================================================
// Runtime demonstration (just prints the compile-time computed values)
// ============================================================================

int main() {
  std::cout << "=== Compile-Time Bit Fields Demo ===\n\n";

  std::cout << "All values below were computed at compile time!\n\n";

  std::cout << "VLAN Tag:\n";
  std::cout << "  PCP: " << ParsedVlan[0] << "\n";
  std::cout << "  DEI: " << ParsedVlan[1] << "\n";
  std::cout << "  VID: " << ParsedVlan[2] << "\n\n";

  std::cout << "IPv4 Header:\n";
  std::cout << "  Version: " << ParsedIpv4[0] << "\n";
  std::cout << "  IHL: " << ParsedIpv4[1] << "\n";
  std::cout << "  TTL: " << ParsedIpv4[8] << "\n";
  std::cout << "  Protocol: " << ParsedIpv4[9] << " (TCP)\n";
  std::cout << "  Src IP: " << std::hex << ParsedIpv4[11] << std::dec << "\n";
  std::cout << "  Dst IP: " << std::hex << ParsedIpv4[12] << std::dec << "\n\n";

  std::cout << "PCIe Extended Capability (little-endian):\n";
  std::cout << "  Capability ID: 0x" << std::hex << ParsedPcieCap[0] << std::dec << "\n";
  std::cout << "  Version: " << ParsedPcieCap[1] << "\n";
  std::cout << "  Next Offset: 0x" << std::hex << ParsedPcieCap[2] << std::dec << "\n\n";

  std::cout << "Custom Format:\n";
  std::cout << "  field_a: 0x" << std::hex << ParsedCustom[0] << "\n";
  std::cout << "  field_b: 0x" << ParsedCustom[1] << "\n";
  std::cout << "  field_c: 0x" << ParsedCustom[2] << "\n";
  std::cout << "  field_d: 0x" << ParsedCustom[3] << std::dec << "\n\n";

  std::cout << "Round-trip verification: " << (BuiltVlan == VlanData ? "PASSED" : "FAILED") << "\n";

  return 0;
}
