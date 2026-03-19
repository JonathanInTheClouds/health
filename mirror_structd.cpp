#include “packet.h”
#include “DataPackageStruct.h”
#include <cstring>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
//  OPTION 2 — Local packed mirror structs
//
//  We define our own tightly packed versions of Preamble, Header, and Payload.
//  We copy the API data into those mirrors field by field, then memcpy the
//  whole mirror struct into the buffer in one shot.
//
//  The mirror structs live only in this file — they are not the API structs.
//  Their only job is to be a padding-free version we can safely bulk-copy.
//
//  Tradeoff: cleaner memcpy calls, but you must keep the mirror fields
//  in sync manually if the API structs ever change.
// ─────────────────────────────────────────────────────────────────────────────

#pragma pack(push, 1)

struct WirePreamble {
uint16_t magic;
uint8_t  version;
uint16_t header_size;
uint16_t payload_size;
uint16_t payload_count;
uint32_t message_length;
};

struct WireHeader {
uint32_t session_id;
uint16_t sequence_num;
uint16_t source_id;
uint16_t dest_id;
uint8_t  message_type;
uint8_t  flags;
int16_t  priority;
int32_t  timestamp_offset;
uint32_t checksum;
};

struct WirePayload {
uint16_t tag;
uint8_t  status;
uint8_t  reserved;
int32_t  value_signed;
uint32_t value_unsigned;
float    value_float;
uint32_t timestamp;
};

#pragma pack(pop)

bool serialize_mirror_structs(const Packet& pkt, DataBus::DataPackageStruct& dds_pkg)
{
const Preamble&             pre      = pkt.preamble;
const Header&               header   = pkt.header;
const std::vector<Payload>& payloads = pkt.payloads;

```
// --- 1. Fill descriptor fields on the DDS struct ---
dds_pkg.source_id           = header.routing.source_id;
dds_pkg.dest_id             = header.routing.dest_id;
dds_pkg.sequence_num        = header.routing.sequence_num;
dds_pkg.message_type        = header.routing.message_type;
dds_pkg.header_size         = pre.header_size;
dds_pkg.payload_record_size = pre.payload_size;
dds_pkg.payload_count       = pre.payload_count;
dds_pkg.message_length      = pre.message_length;

// --- 2. Wire size is now safe to calculate from the packed mirror structs ---
const uint32_t total_bytes =
    static_cast<uint32_t>(sizeof(WirePreamble))                    +
    static_cast<uint32_t>(sizeof(WireHeader))                      +
    static_cast<uint32_t>(payloads.size() * sizeof(WirePayload));

// --- 3. Allocate the octet buffer ---
if (!dds_pkg.payload.ensure_length(total_bytes, total_bytes)) {
    printf("ERROR: failed to allocate octet buffer (%u bytes)\n", total_bytes);
    return false;
}

uint8_t*  buf    = dds_pkg.payload.get_contiguous_buffer();
uint32_t  offset = 0;

// --- 4. Populate mirror structs from API structs field by field ---
//        This is where the "translation" happens

WirePreamble wp;
wp.magic          = pre.magic;
wp.version        = pre.version;
wp.header_size    = pre.header_size;
wp.payload_size   = pre.payload_size;
wp.payload_count  = pre.payload_count;
wp.message_length = pre.message_length;

WireHeader wh;
wh.source_id        = header.routing.source_id;
wh.dest_id          = header.routing.dest_id;
wh.sequence_num     = header.routing.sequence_num;
wh.message_type     = header.routing.message_type;
wh.session_id       = header.session.session_id;
wh.timestamp_offset = header.session.timestamp_offset;
wh.checksum         = header.session.checksum;
wh.flags            = header.control.flags;
wh.priority         = header.control.priority;

// --- 5. memcpy the packed mirrors into the buffer in one shot ---
memcpy(buf + offset, &wp, sizeof(WirePreamble));
offset += sizeof(WirePreamble);

memcpy(buf + offset, &wh, sizeof(WireHeader));
offset += sizeof(WireHeader);

// --- 6. For each payload, populate a WirePayload and memcpy it in ---
for (const Payload& p : payloads) {
    WirePayload wpay;
    wpay.tag            = p.tag;
    wpay.status         = p.status;
    wpay.reserved       = p.reserved;
    wpay.value_signed   = p.value_signed;
    wpay.value_unsigned = p.value_unsigned;
    wpay.value_float    = p.value_float;
    wpay.timestamp      = p.timestamp;

    memcpy(buf + offset, &wpay, sizeof(WirePayload));
    offset += sizeof(WirePayload);
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
pre.header_size    = static_cast<uint16_t>(sizeof(WireHeader));   // note: wire size now
pre.payload_size   = static_cast<uint16_t>(sizeof(WirePayload));  // note: wire size now
pre.payload_count  = static_cast<uint16_t>(payloads.size());
pre.message_length = static_cast<uint32_t>(
                         sizeof(WirePreamble) +
                         sizeof(WireHeader)   +
                         payloads.size() * sizeof(WirePayload));

Packet pkt;
pkt.preamble = pre;
pkt.header   = header;
pkt.payloads = payloads;

DataBus::DataPackageStruct dds_pkg;
if (serialize_mirror_structs(pkt, dds_pkg)) {
    printf("Option 2 success!\n");
    printf("  Octet buffer length : %u bytes\n", dds_pkg.payload.length());
    printf("  sizeof(WirePreamble): %zu\n", sizeof(WirePreamble));
    printf("  sizeof(WireHeader)  : %zu\n", sizeof(WireHeader));
    printf("  sizeof(WirePayload) : %zu\n", sizeof(WirePayload));
}

return 0;
```

}