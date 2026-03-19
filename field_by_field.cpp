#include “packet.h”
#include “DataPackageStruct.h”
#include <cstring>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
//  OPTION 1 — Field by field serialization
//
//  We never memcpy a whole struct at once. Instead we copy each field
//  individually. This means padding bytes in the source struct are
//  completely irrelevant — we only ever touch the actual data fields.
// ─────────────────────────────────────────────────────────────────────────────

// Helper macro to cut down on repetition —
// copies one field into the buffer and advances the offset
#define COPY_FIELD(buf, offset, field)   
memcpy((buf) + (offset), &(field), sizeof(field));   
(offset) += sizeof(field);

bool serialize_field_by_field(const Packet& pkt, DataBus::DataPackageStruct& dds_pkg)
{
const Preamble&             pre      = pkt.preamble;
const Header&               header   = pkt.header;
const std::vector<Payload>& payloads = pkt.payloads;

```
// --- 1. Fill descriptor fields on the DDS struct ---
//        Now accessing fields through the nested structs
dds_pkg.source_id           = header.routing.source_id;
dds_pkg.dest_id             = header.routing.dest_id;
dds_pkg.sequence_num        = header.routing.sequence_num;
dds_pkg.message_type        = header.routing.message_type;
dds_pkg.header_size         = pre.header_size;
dds_pkg.payload_record_size = pre.payload_size;
dds_pkg.payload_count       = pre.payload_count;
dds_pkg.message_length      = pre.message_length;

// --- 2. Calculate the true wire size field by field ---
//        We drill into each nested struct individually
const uint32_t preamble_wire_size =
    sizeof(pre.magic)          +
    sizeof(pre.version)        +
    sizeof(pre.header_size)    +
    sizeof(pre.payload_size)   +
    sizeof(pre.payload_count)  +
    sizeof(pre.message_length);

// RoutingInfo fields
const uint32_t routing_wire_size =
    sizeof(header.routing.source_id)    +
    sizeof(header.routing.dest_id)      +
    sizeof(header.routing.sequence_num) +
    sizeof(header.routing.message_type) +
    sizeof(header.routing.reserved);

// SessionInfo fields
const uint32_t session_wire_size =
    sizeof(header.session.session_id)       +
    sizeof(header.session.timestamp_offset) +
    sizeof(header.session.checksum);

// ControlInfo fields
const uint32_t control_wire_size =
    sizeof(header.control.flags)    +
    sizeof(header.control.priority) +
    sizeof(header.control.reserved);

const uint32_t header_wire_size =
    routing_wire_size +
    session_wire_size +
    control_wire_size;

const uint32_t payload_wire_size =
    sizeof(Payload::tag)            +
    sizeof(Payload::status)         +
    sizeof(Payload::reserved)       +
    sizeof(Payload::value_signed)   +
    sizeof(Payload::value_unsigned) +
    sizeof(Payload::value_float)    +
    sizeof(Payload::timestamp);

const uint32_t total_bytes =
    preamble_wire_size +
    header_wire_size   +
    static_cast<uint32_t>(payloads.size()) * payload_wire_size;

// --- 3. Allocate the octet buffer ---
if (!dds_pkg.payload.ensure_length(total_bytes, total_bytes)) {
    printf("ERROR: failed to allocate octet buffer (%u bytes)\n", total_bytes);
    return false;
}

uint8_t*  buf    = dds_pkg.payload.get_contiguous_buffer();
uint32_t  offset = 0;

// --- 4. Copy preamble fields one by one ---
COPY_FIELD(buf, offset, pre.magic);
COPY_FIELD(buf, offset, pre.version);
COPY_FIELD(buf, offset, pre.header_size);
COPY_FIELD(buf, offset, pre.payload_size);
COPY_FIELD(buf, offset, pre.payload_count);
COPY_FIELD(buf, offset, pre.message_length);

// --- 5. Copy header fields — drilling into each nested struct ---

// RoutingInfo
COPY_FIELD(buf, offset, header.routing.source_id);
COPY_FIELD(buf, offset, header.routing.dest_id);
COPY_FIELD(buf, offset, header.routing.sequence_num);
COPY_FIELD(buf, offset, header.routing.message_type);
COPY_FIELD(buf, offset, header.routing.reserved);

// SessionInfo
COPY_FIELD(buf, offset, header.session.session_id);
COPY_FIELD(buf, offset, header.session.timestamp_offset);
COPY_FIELD(buf, offset, header.session.checksum);

// ControlInfo
COPY_FIELD(buf, offset, header.control.flags);
COPY_FIELD(buf, offset, header.control.priority);
COPY_FIELD(buf, offset, header.control.reserved);

// --- 6. Copy each payload's fields one by one ---
for (const Payload& p : payloads) {
    COPY_FIELD(buf, offset, p.tag);
    COPY_FIELD(buf, offset, p.status);
    COPY_FIELD(buf, offset, p.reserved);
    COPY_FIELD(buf, offset, p.value_signed);
    COPY_FIELD(buf, offset, p.value_unsigned);
    COPY_FIELD(buf, offset, p.value_float);
    COPY_FIELD(buf, offset, p.timestamp);
}

// --- 7. Sanity check ---
if (offset != total_bytes) {
    printf("ERROR: offset mismatch — wrote %u bytes, expected %u\n", offset, total_bytes);
    return false;
}

return true;
```

}

int main()
{
// — Simulated: header + payloads already loaded from API —
Header header;
header.routing.source_id        = 0x0001;
header.routing.dest_id          = 0x0002;
header.routing.sequence_num     = 7;
header.routing.message_type     = 0x01;
header.routing.reserved         = 0;
header.session.session_id       = 0xCAFEBABE;
header.session.timestamp_offset = 0;
header.session.checksum         = 0;
header.control.flags            = 0x00;
header.control.priority         = 5;
header.control.reserved         = 0;

```
std::vector<Payload> payloads(4);
for (int i = 0; i < 4; ++i) {
    payloads[i].tag             = static_cast<uint16_t>(0x0A00 + i);
    payloads[i].status          = 0;
    payloads[i].reserved        = 0;
    payloads[i].value_signed    = -100 + (i * 10);
    payloads[i].value_unsigned  = i * 42u;
    payloads[i].value_float     = 3.14f * i;
    payloads[i].timestamp       = 1700000000u + i;
}

Preamble pre;
pre.magic          = 0xDEAD;
pre.version        = 1;
pre.header_size    = static_cast<uint16_t>(sizeof(Header));
pre.payload_size   = static_cast<uint16_t>(sizeof(Payload));
pre.payload_count  = static_cast<uint16_t>(payloads.size());
pre.message_length = static_cast<uint32_t>(
                         sizeof(Preamble) +
                         sizeof(Header)   +
                         payloads.size() * sizeof(Payload));

Packet pkt;
pkt.preamble = pre;
pkt.header   = header;
pkt.payloads = payloads;

DataBus::DataPackageStruct dds_pkg;
if (serialize_field_by_field(pkt, dds_pkg)) {
    printf("Option 1 success!\n");
    printf("  Octet buffer length : %u bytes\n", dds_pkg.payload.length());
}

return 0;
```

}