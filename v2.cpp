// ============================================================
//  message_capture.cpp
//
//  Demonstrates Pattern 1: Plain Function Pointer callback.
//
//  File is split into three logical sections:
//    [1] lib.h      — what the library ships to you (simulated here)
//    [2] capture.h  — your capture layer (MessageStore + writeSomething)
//    [3] main()     — wires everything together
//
//  Compile:
//    g++ -std=c++17 -pthread -o message_capture message_capture.cpp && ./message_capture
// ============================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ============================================================
//  [1]  lib.h  — Simulated library (pretend this is their code)
//
//  In reality this would be a header + compiled .so/.dll you
//  link against. We define it inline here just so the example
//  compiles and runs on its own.
// ============================================================

// The exact callback signature the library requires.
// You must match this exactly — return type, parameter types, order.
typedef void (*WriterFn)(int messageType, std::ostream*, uint8_t* buf, size_t nbytes);

// Internal library state
namespace lib_internal {
WriterFn g_registeredWriter = nullptr; // starts as null — nothing registered yet
}

// The library’s registration function.
// Call this once at startup to hand your function to the library.
void lib_registerWriter(WriterFn fn) {
lib_internal::g_registeredWriter = fn;
std::cout << “[LIB] Writer registered.\n”;
}

// The library’s start function.
// Simulates the library firing the callback several times
// as if it were processing incoming data from its source.
void lib_start() {
if (lib_internal::g_registeredWriter == nullptr) {
std::cout << “[LIB] No writer registered — nothing to do.\n”;
return;
}

```
std::cout << "[LIB] Starting — will emit 5 messages...\n\n";

// Build a fake buffer each "emit": header + small payload
auto makeBuffer = [](uint32_t magic, uint8_t fillByte, size_t payloadSize)
    -> std::vector<uint8_t>
{
    struct Hdr {
        uint32_t magic;
        uint16_t version;
        uint16_t flags;
        uint32_t record_count;
        uint32_t record_size;
        uint64_t payload_bytes;
    } __attribute__((packed));

    std::vector<uint8_t> buf(sizeof(Hdr) + payloadSize, 0);

    Hdr* hdr           = reinterpret_cast<Hdr*>(buf.data());
    hdr->magic         = magic;
    hdr->version       = 1;
    hdr->flags         = 0x0001;
    hdr->record_count  = 2;
    hdr->record_size   = static_cast<uint32_t>(payloadSize / 2);
    hdr->payload_bytes = payloadSize;

    for (size_t i = 0; i < payloadSize; ++i)
        buf[sizeof(Hdr) + i] = static_cast<uint8_t>(fillByte + i);

    return buf;
};

// Simulate 5 callback firings with different message types
struct Emit { int type; uint32_t magic; uint8_t fill; size_t payloadSize; };
Emit emits[] = {
    { 1, 0xAABBCCDD,  0x10,  8 },   // Telemetry
    { 3, 0x11223344,  0x20, 16 },   // Event
    { 2, 0xDEADBEEF,  0x30,  4 },   // Diagnostic
    { 3, 0x55667788,  0x40, 12 },   // Event (second one)
    { 99, 0xFFFFFFFF, 0x00,  8 },   // Unknown type — should be dropped
};

for (const auto& e : emits) {
    auto buf = makeBuffer(e.magic, e.fill, e.payloadSize);

    std::cout << "[LIB] Firing callback — messageType=" << e.type
              << "  bytes=" << buf.size() << "\n";

    // This is the library calling YOUR function via the stored pointer
    lib_internal::g_registeredWriter(e.type, nullptr, buf.data(), buf.size());
}

std::cout << "\n[LIB] Done emitting.\n\n";
```

}

// ============================================================
//  [2]  capture.h  — Your capture layer
// ============================================================

enum class MessageType : int {
Telemetry  = 1,
Diagnostic = 2,
Event      = 3,
Payload    = 4
};

inline std::string messageTypeName(int type) {
switch (static_cast<MessageType>(type)) {
case MessageType::Telemetry:  return “Telemetry”;
case MessageType::Diagnostic: return “Diagnostic”;
case MessageType::Event:      return “Event”;
case MessageType::Payload:    return “Payload”;
default:                      return “Unknown”;
}
}

#pragma pack(push, 1)
struct MessageHeader {
uint32_t magic;
uint16_t version;
uint16_t flags;
uint32_t record_count;
uint32_t record_size;
uint64_t payload_bytes;
};
#pragma pack(pop)

struct CapturedMessage {
int                  messageType;
MessageHeader        header;
std::vector<uint8_t> payload;
};

class MessageStore {
public:
static const std::unordered_set<int> knownTypes;

```
void capture(int messageType, const uint8_t* buf, size_t nbytes) {
    if (knownTypes.find(messageType) == knownTypes.end()) {
        std::cout << "[CAPTURE] Dropped unknown messageType=" << messageType << "\n";
        return;
    }
    if (buf == nullptr || nbytes < sizeof(MessageHeader)) {
        std::cout << "[CAPTURE] Dropped malformed buffer\n";
        return;
    }

    CapturedMessage msg;
    msg.messageType = messageType;
    std::memcpy(&msg.header, buf, sizeof(MessageHeader));

    const size_t payloadOffset = sizeof(MessageHeader);
    const size_t payloadSize   = nbytes - payloadOffset;
    if (payloadSize > 0)
        msg.payload.assign(buf + payloadOffset, buf + payloadOffset + payloadSize);

    std::lock_guard<std::mutex> lock(mutex_);
    store_[messageType].push_back(std::move(msg));

    std::cout << "[CAPTURE] Stored messageType=" << messageType
              << " (" << messageTypeName(messageType) << ")"
              << "  payload=" << payloadSize << " bytes\n";
}

std::vector<CapturedMessage> getMessages(MessageType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(static_cast<int>(type));
    if (it == store_.end()) return {};
    return it->second;
}

size_t countOf(MessageType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(static_cast<int>(type));
    return (it != store_.end()) ? it->second.size() : 0;
}

size_t totalCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = 0;
    for (const auto& kv : store_) n += kv.second.size();
    return n;
}

void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    store_.clear();
}

void printSummary(std::ostream& out = std::cout) const {
    std::lock_guard<std::mutex> lock(mutex_);
    out << "=== MessageStore Summary ===\n";
    for (const auto& kv : store_) {
        out << "  [" << kv.first << "] "
            << messageTypeName(kv.first)
            << " : " << kv.second.size() << " message(s)\n";
    }
    out << "  Total : " << totalCount() << " message(s)\n";
    out << "============================\n";
}
```

private:
mutable std::mutex                                    mutex_;
std::unordered_map<int, std::vector<CapturedMessage>> store_;
};

const std::unordered_set<int> MessageStore::knownTypes = {
static_cast<int>(MessageType::Telemetry),
static_cast<int>(MessageType::Diagnostic),
static_cast<int>(MessageType::Event),
static_cast<int>(MessageType::Payload)
};

static MessageStore g_messageStore;

// The function whose ADDRESS gets passed to lib_registerWriter()
void writeSomething(int messageType, std::ostream* /*unused*/, uint8_t* buf, size_t nbytes) {
g_messageStore.capture(messageType, buf, nbytes);
}

std::vector<CapturedMessage> getMessages(MessageType type) {
return g_messageStore.getMessages(type);
}
size_t getMessageCount(MessageType type) { return g_messageStore.countOf(type); }
void   clearMessages()                   { g_messageStore.clear(); }
void   printStoreSummary()               { g_messageStore.printSummary(); }

// ============================================================
//  [3]  main() — wires the library to your capture layer
// ============================================================

int main() {
std::cout << “=== Pattern 1: Plain Function Pointer ===\n\n”;

```
// Step 1 — Pass the ADDRESS of writeSomething to the library.
//           The library stores it internally as:
//             WriterFn g_registeredWriter = writeSomething;
lib_registerWriter(writeSomething);

std::cout << "\n";

// Step 2 — Start the library. Internally it calls:
//             g_registeredWriter(type, stream, buf, nbytes)
//           which resolves to:
//             writeSomething(type, stream, buf, nbytes)
lib_start();

// Step 3 — Inspect what was captured
printStoreSummary();

std::cout << "\n--- Event messages ---\n";
auto events = getMessages(MessageType::Event);
for (size_t i = 0; i < events.size(); ++i) {
    const auto& msg = events[i];
    std::cout << "  [" << i << "]"
              << "  magic=0x"      << std::hex << msg.header.magic
              << "  version="      << std::dec << msg.header.version
              << "  payload="      << msg.payload.size() << " bytes"
              << "  first_byte=0x" << std::hex
              << (msg.payload.empty() ? 0 : (int)msg.payload[0])
              << "\n";
}
std::cout << std::dec;

std::cout << "\n--- Telemetry messages ---\n";
auto telemetry = getMessages(MessageType::Telemetry);
for (size_t i = 0; i < telemetry.size(); ++i) {
    const auto& msg = telemetry[i];
    std::cout << "  [" << i << "]"
              << "  magic=0x" << std::hex << msg.header.magic
              << "  payload=" << std::dec << msg.payload.size() << " bytes\n";
}

return 0;
```

}