#pragma once

#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────
//  Ensure no padding bytes between fields
//  (important for wire/binary protocol structs)
// ─────────────────────────────────────────────
#pragma pack(push, 1)

// ─────────────────────────────────────────────
//  PREAMBLE
//  Fixed-size prefix that describes what follows
// ─────────────────────────────────────────────
struct Preamble {
uint16_t  magic;            // Sync/magic bytes e.g. 0xDEAD
uint8_t   version;          // Protocol version
uint16_t  header_size;      // Size of the Header struct in bytes
uint16_t  payload_size;     // Size of ONE Payload struct in bytes
uint16_t  payload_count;    // N — how many payloads follow
uint32_t  message_length;   // Total message size (preamble + header + all payloads)
};

// ─────────────────────────────────────────────
//  HEADER — contains 3 nested structs
// ─────────────────────────────────────────────

// Identifies who is talking to who and in what order
struct RoutingInfo {
uint16_t  source_id;        // Sender identifier
uint16_t  dest_id;          // Receiver identifier
uint16_t  sequence_num;     // Packet sequence number
uint8_t   message_type;     // Category/type of message
uint8_t   reserved;         // Padding / future use
};

// Session-level context for the message
struct SessionInfo {
uint32_t  session_id;       // Identifies the session
int32_t   timestamp_offset; // Signed offset from epoch in ms
uint32_t  checksum;         // CRC or simple sum over header+payloads
};

// Control and priority signaling
struct ControlInfo {
uint8_t   flags;            // Bit flags (e.g. 0x01 = ack, 0x02 = frag)
int16_t   priority;         // Signed: negative = low, positive = high
uint8_t   reserved;         // Padding / future use
};

// Header nests all three by value
struct Header {
RoutingInfo  routing;
SessionInfo  session;
ControlInfo  control;
};

// ─────────────────────────────────────────────
//  PAYLOAD  (repeated N times)
//  One unit of application data
// ─────────────────────────────────────────────
struct Payload {
uint16_t  tag;              // Identifies what kind of data this is
uint8_t   status;           // 0=ok, 1=warn, 2=error etc.
uint8_t   reserved;         // Padding / future use — keep alignment clean
int32_t   value_signed;     // e.g. temperature, offset, delta
uint32_t  value_unsigned;   // e.g. counter, raw sensor reading
float     value_float;      // e.g. voltage, ratio
uint32_t  timestamp;        // When this payload was captured
};

#pragma pack(pop)

// ─────────────────────────────────────────────
//  PACKET  (the whole message in one place)
// ─────────────────────────────────────────────
struct Packet {
Preamble             preamble;
Header               header;
std::vector<Payload> payloads;  // size == preamble.payload_count
};