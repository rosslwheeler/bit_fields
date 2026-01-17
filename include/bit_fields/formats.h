#pragma once

#include "bit_fields/bitstream.h"

namespace bit_fields::formats {

// ============================================================================
// Pre-defined packet formats for common network protocols
// ============================================================================

/// VLAN tag (802.1Q) - 16 bits after EtherType 0x8100
inline constexpr PacketFormat<3> kVlanTag{{{
    FieldDef{"pcp", 3},  // Priority Code Point
    FieldDef{"dei", 1},  // Drop Eligible Indicator
    FieldDef{"vid", 12},  // VLAN Identifier
}}};

/// Destination MAC address with explicit I/G and U/L bits (IEEE 802.3)
/// 48 bits: [I/G (1), U/L (1), rest (46)]
inline constexpr PacketFormat<3> kDestMacWithExplicitBits{{{
    FieldDef{"ig_bit", 1},  // Individual/Group bit (MSB)
    FieldDef{"ul_bit", 1},  // Universal/Local bit
    FieldDef{"mac_rest", 46}  // Remaining MAC bits
}}};

/// Ethernet header (without VLAN) - 112 bits
inline constexpr PacketFormat<3> kEthernetHeader{{{
    FieldDef{"dest_mac", 48},
    FieldDef{"src_mac", 48},
    FieldDef{"ethertype", 16},
}}};

/// IPv4 header (fixed portion) - 160 bits
inline constexpr PacketFormat<13> kIpv4Header{{{
    FieldDef{"version", 4},
    FieldDef{"ihl", 4},
    FieldDef{"dscp", 6},
    FieldDef{"ecn", 2},
    FieldDef{"total_length", 16},
    FieldDef{"identification", 16},
    FieldDef{"flags", 3},
    FieldDef{"fragment_offset", 13},
    FieldDef{"ttl", 8},
    FieldDef{"protocol", 8},
    FieldDef{"header_checksum", 16},
    FieldDef{"src_ip", 32},
    FieldDef{"dst_ip", 32},
}}};

/// TCP header (fixed portion, no options) - 160 bits
inline constexpr PacketFormat<10> kTcpHeader{{{
    FieldDef{"src_port", 16},
    FieldDef{"dst_port", 16},
    FieldDef{"seq_num", 32},
    FieldDef{"ack_num", 32},
    FieldDef{"data_offset", 4},
    FieldDef{"reserved", 3},
    FieldDef{"flags", 9},
    FieldDef{"window_size", 16},
    FieldDef{"checksum", 16},
    FieldDef{"urgent_ptr", 16},
}}};

/// UDP header - 64 bits
inline constexpr PacketFormat<4> kUdpHeader{{{
    FieldDef{"src_port", 16},
    FieldDef{"dst_port", 16},
    FieldDef{"length", 16},
    FieldDef{"checksum", 16},
}}};

/// ARP header (for Ethernet/IPv4) - 224 bits
inline constexpr PacketFormat<9> kArpHeader{{{
    FieldDef{"hw_type", 16},
    FieldDef{"proto_type", 16},
    FieldDef{"hw_addr_len", 8},
    FieldDef{"proto_addr_len", 8},
    FieldDef{"operation", 16},
    FieldDef{"sender_hw_addr", 48},
    FieldDef{"sender_proto_addr", 32},
    FieldDef{"target_hw_addr", 48},
    FieldDef{"target_proto_addr", 32},
}}};

/// ICMP header (common portion) - 64 bits
inline constexpr PacketFormat<4> kIcmpHeader{{{
    FieldDef{"type", 8},
    FieldDef{"code", 8},
    FieldDef{"checksum", 16},
    FieldDef{"rest_of_header", 32},  // Interpretation depends on type/code
}}};

// ============================================================================
// PCIe / PCI formats
// ============================================================================

/// PCI Extended Capability Header - 32 bits
inline constexpr PacketFormat<3> kPcieExtCapHeader{{{
    FieldDef{"capability_id", 16},
    FieldDef{"version", 4},
    FieldDef{"next_offset", 12},
}}};

/// PCI Standard Capability Header - 16 bits
inline constexpr PacketFormat<2> kPciCapHeader{{{
    FieldDef{"capability_id", 8},
    FieldDef{"next_ptr", 8},
}}};

/// PCI BAR (Memory, 32-bit) - 32 bits
inline constexpr PacketFormat<4> kPciMemoryBar32{{{
    FieldDef{"io_space", 1},  // 0 = memory
    FieldDef{"type", 2},  // 00 = 32-bit, 10 = 64-bit
    FieldDef{"prefetchable", 1},
    FieldDef{"base_address", 28},
}}};

/// PCI BAR (I/O) - 32 bits
inline constexpr PacketFormat<3> kPciIoBar{{{
    FieldDef{"io_space", 1},  // 1 = I/O
    FieldDef{"reserved", 1},
    FieldDef{"base_address", 30},
}}};

}  // namespace bit_fields::formats
