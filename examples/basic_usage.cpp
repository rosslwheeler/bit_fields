/// @file basic_usage.cpp
/// @brief Basic usage examples for the bit_fields library.

#include <array>
#include <cstdint>
#include <iostream>
#include <span>

#include "bit_fields/bit_fields.h"
#include "bit_fields/constexpr_bitstream.h"

using namespace bit_fields;

/// Example 1: Deserialize VLAN tag into a result object (access by name or index)
void example_deserialize_vlan(std::span<const std::byte> data) {
  NetworkBitReader reader(data);

  auto vlan = reader.deserialize(formats::kVlanTag);

  // Access by name (runtime lookup)
  auto vid = vlan.get("vid");
  std::cout << "VLAN ID: " << vid << "\n";

  // Access by index (compile-time, zero overhead)
  auto pcp = vlan[0];
  auto dei = vlan[1];
  std::cout << "PCP: " << pcp << ", DEI: " << dei << "\n";
}

/// Example 2: Deserialize directly into variables
void example_deserialize_vlan_direct(std::span<const std::byte> data) {
  NetworkBitReader reader(data);

  std::uint8_t pcp  = 0;
  std::uint8_t dei  = 0;
  std::uint16_t vid = 0;

  // Deserialize straight into your variables
  reader.deserialize_into(formats::kVlanTag, pcp, dei, vid);

  std::cout << "PCP: " << static_cast<int>(pcp) << ", DEI: " << static_cast<int>(dei)
            << ", VID: " << vid << "\n";
}

/// Example 3: Serialize values into a buffer
void example_build_vlan(std::span<std::byte> buffer,
                        std::uint8_t pcp,
                        bool dei,
                        std::uint16_t vid) {
  NetworkBitWriter writer(buffer);

  writer.serialize(formats::kVlanTag, pcp, dei ? 1 : 0, vid);

  std::cout << "Wrote " << writer.bytes_written() << " bytes\n";
}

/// Example 4: Full Ethernet + VLAN deserializing
void example_deserialize_ethernet_frame(std::span<const std::byte> frame) {
  NetworkBitReader reader(frame);

  // Deserialize Ethernet header
  auto eth       = reader.deserialize(formats::kEthernetHeader);
  auto ethertype = eth.get("ethertype");

  std::cout << "Dest MAC: " << std::hex << eth.get("dest_mac") << "\n";
  std::cout << "Src MAC: " << std::hex << eth.get("src_mac") << "\n";
  std::cout << "EtherType: 0x" << std::hex << ethertype << std::dec << "\n";

  // Check for VLAN tag
  if (ethertype == 0x8100) {
    auto vlan = reader.deserialize(formats::kVlanTag);
    std::cout << "VLAN ID: " << vlan.get("vid") << "\n";

    // Read real EtherType
    ethertype = reader.read_aligned<std::uint16_t>();
    std::cout << "Inner EtherType: 0x" << std::hex << ethertype << std::dec << "\n";
  }

  // Continue based on EtherType...
  if (ethertype == 0x0800) {
    auto ipv4 = reader.deserialize(formats::kIpv4Header);
    std::cout << "IPv4 Version: " << ipv4.get("version") << "\n";
    std::cout << "TTL: " << ipv4.get("ttl") << "\n";
    std::cout << "Protocol: " << ipv4.get("protocol") << "\n";
  }
}
// Example 4: Ethernet + VLAN header parsing using constexpr_bitstream
void example_ethernet_vlan() {
  constexpr std::array<std::byte, 34> eth_vlan_ipv4 = [] {
    using namespace bit_fields;
    using namespace bit_fields::constexpr_formats;
    ConstexprBitWriter<34> writer;
    writer.serialize(kEthernetHeader,
                     0x112233445566ULL,  // dest_mac
                     0xAABBCCDDEEFFULL,  // src_mac
                     0x8100);  // ethertype (VLAN)
    writer.serialize(kVlanTag, 3, 1, 42);
    writer.write_bits(16, 0x0800);  // inner ethertype (IPv4)
    writer.serialize(kIpv4Header, 4, 5, 0, 0, 40, 0, 0, 0, 64, 6, 0, 0xC0A80101, 0xC0A80102);
    return writer.data();
  }();
  example_deserialize_ethernet_frame(eth_vlan_ipv4);
}

/// Example 5: Manual bit-level reading (without format metadata)
void example_manual_bits(std::span<const std::byte> data) {
  NetworkBitReader reader(data);

  // Read individual fields manually
  auto field1 = reader.read_bits<4>();  // 4 bits
  auto field2 = reader.read_bits<12>();  // 12 bits
  auto field3 = reader.read_bits<16>();  // 16 bits

  std::cout << "Field1: " << field1 << ", Field2: " << field2 << ", Field3: " << field3 << "\n";

  // Skip some bits
  reader.skip_bits(8);

  // Align to byte boundary
  reader.align_to_byte();

  // Read a byte-aligned value (fast path)
  auto value = reader.read_aligned<std::uint32_t>();
  std::cout << "Aligned u32: " << value << "\n";
}

/// Example 6: PCIe Extended Capability Header
void example_pcie_ext_cap(std::span<const std::byte> data) {
  BitReader<WireOrder::LittleEndian> reader(data);  // PCIe is little-endian

  auto cap = reader.deserialize(formats::kPcieExtCapHeader);

  std::cout << "Capability ID: 0x" << std::hex << cap.get("capability_id") << "\n";
  std::cout << "Version: " << std::dec << cap.get("version") << "\n";
  std::cout << "Next Offset: 0x" << std::hex << cap.get("next_offset") << "\n";
}

/// Example 7: Accessing a payload field using span_of_field (read)
void example_read_payload_span(std::span<const std::byte> data) {
  constexpr PacketFormat<3> kWithPayload{
      {std::to_array<FieldDef>({{"a", 8}, {"b", 8}, {"payload", 16}})}};
  NetworkBitReader reader(data);
  auto payload = reader.span_of_field(kWithPayload, "payload");
  if (!payload.empty()) {
    std::cout << "Payload bytes: ";
    for (auto b : payload)
      std::cout << std::hex << int(b) << ' ';
    std::cout << std::dec << "\n";
  } else {
    std::cout << "Payload field not found or not byte-aligned\n";
  }
}

/// Example 8: Accessing a payload field using span_of_field (write)
void example_write_payload_span(std::span<std::byte> data) {
  constexpr PacketFormat<3> kWithPayload{
      {std::to_array<FieldDef>({{"a", 8}, {"b", 8}, {"payload", 16}})}};
  NetworkBitWriter writer(data);
  auto payload = writer.span_of_field(kWithPayload, "payload");
  if (!payload.empty()) {
    payload[0] = std::byte{0xAB};
    payload[1] = std::byte{0xCD};
    std::cout << "Payload written: " << std::hex << int(payload[0]) << ' ' << int(payload[1])
              << std::dec << "\n";
  } else {
    std::cout << "Payload field not found or not byte-aligned\n";
  }
}

void example_pcie_extcap() {
  std::cout << "\n=== Example 6: PCIe Extended Capability Header ===\n";
  // cap_id=0x0010, version=2, next_offset=0x040
  std::array<std::byte, 4> pcie_extcap = {
      std::byte{0x10}, std::byte{0x00}, std::byte{0x24}, std::byte{0x00}};
  example_pcie_ext_cap(pcie_extcap);
}

void example_read_payload_span_fn() {
  std::cout << "\n=== Example 7: Read Payload Span ===\n";
  std::array<std::byte, 4> payload_data = {
      std::byte{0x01}, std::byte{0x02}, std::byte{0xAA}, std::byte{0xBB}};
  example_read_payload_span(payload_data);
}

void example_write_payload_span_fn() {
  std::cout << "\n=== Example 8: Write Payload Span ===\n";
  std::array<std::byte, 4> payload_out = {
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
  example_write_payload_span(payload_out);
  std::cout << "Payload buffer after write: " << std::hex << int(payload_out[2]) << ' '
            << int(payload_out[3]) << std::dec << "\n";
}

int main() {
  // Sample VLAN tag data: PCP=5, DEI=0, VID=100
  // Binary: 101 0 0000 0110 0100 = 0xA064
  std::array<std::byte, 2> vlan_data = {std::byte{0xA0}, std::byte{0x64}};

  std::cout << "=== Example 1: Deserialize VLAN ===\n";
  example_deserialize_vlan(vlan_data);

  std::cout << "\n=== Example 2: Deserialize VLAN Direct ===\n";
  example_deserialize_vlan_direct(vlan_data);

  std::cout << "\n=== Example 3: Build VLAN ===\n";
  std::array<std::byte, 2> output_buffer{};
  example_build_vlan(output_buffer, 5, false, 100);

  // Verify round-trip
  std::cout << "Round-trip match: " << std::boolalpha << (vlan_data == output_buffer) << "\n";

  std::cout << "\n=== Example 4: Ethernet + VLAN ===\n";
  example_ethernet_vlan();

  std::cout << "\n=== Example 6: PCIe Extended Capability Header ===\n";
  example_pcie_extcap();

  std::cout << "\n=== Example 7: Read Payload Span ===\n";
  example_read_payload_span_fn();

  std::cout << "\n=== Example 8: Write Payload Span ===\n";
  example_write_payload_span_fn();

  return 0;
}
