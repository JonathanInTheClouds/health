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
//  Message Types
// ============================================================

enum class MessageType : int {
    Telemetry  = 1,
    Diagnostic = 2,
    Event      = 3,
    Payload    = 4
};

inline std::string messageTypeName(int type) {
    switch (static_cast<MessageType>(type)) {
        case MessageType::Telemetry:  return "Telemetry";
        case MessageType::Diagnostic: return "Diagnostic";
        case MessageType::Event:      return "Event";
        case MessageType::Payload:    return "Payload";
        default:                      return "Unknown";
    }
}

// ============================================================
//  Structures
// ============================================================

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

// ============================================================
//  Message Store
// ============================================================

class MessageStore {
public:
    static const std::unordered_set<int> knownTypes;

    // Called from writeSomething() — parses and stores the message
    void capture(int messageType, const uint8_t* buf, size_t nbytes) {
        // Drop unknown types
        if (knownTypes.find(messageType) == knownTypes.end()) return;

        // Need at least a full header to be valid
        if (buf == nullptr || nbytes < sizeof(MessageHeader)) return;

        CapturedMessage msg;
        msg.messageType = messageType;

        // Copy the header out of the raw buffer safely
        std::memcpy(&msg.header, buf, sizeof(MessageHeader));

        // Everything after the header is the payload
        const size_t payloadOffset = sizeof(MessageHeader);
        const size_t payloadSize   = nbytes - payloadOffset;

        if (payloadSize > 0) {
            msg.payload.assign(buf + payloadOffset, buf + payloadOffset + payloadSize);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        store_[messageType].push_back(std::move(msg));
    }

    // Returns a safe copy of all messages for a given type
    std::vector<CapturedMessage> getMessages(MessageType type) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = store_.find(static_cast<int>(type));
        if (it == store_.end()) return {};
        return it->second;
    }

    // Count of messages for a specific type
    size_t countOf(MessageType type) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = store_.find(static_cast<int>(type));
        return (it != store_.end()) ? it->second.size() : 0;
    }

    // Total across all types
    size_t totalCount() const {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t count = 0;
        for (const auto& kv : store_) count += kv.second.size();
        return count;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        store_.clear();
    }

    void clearType(MessageType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        store_.erase(static_cast<int>(type));
    }

    void printSummary(std::ostream& out = std::cout) const {
        std::lock_guard<std::mutex> lock(mutex_);

        out << "=== MessageStore Summary ===\n";
        for (const auto& kv : store_) {
            out << "  [" << kv.first << "] "
                << messageTypeName(kv.first)
                << " : " << kv.second.size() << " message(s)\n";
        }
        out << "  Total: " << totalCount() << "\n";
        out << "============================\n";
    }

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

// ============================================================
//  Global Store + Callback
// ============================================================

static MessageStore g_messageStore;

// This is the function you hand to the library
void writeSomething(int messageType, std::ostream* /*unused*/, uint8_t* buf, size_t nbytes) {
    g_messageStore.capture(messageType, buf, nbytes);
}

// ============================================================
//  Public Accessors (use these instead of touching g_messageStore directly)
// ============================================================

std::vector<CapturedMessage> getMessages(MessageType type) {
    return g_messageStore.getMessages(type);
}

size_t getMessageCount(MessageType type) {
    return g_messageStore.countOf(type);
}

void clearMessages()     { g_messageStore.clear(); }
void printStoreSummary() { g_messageStore.printSummary(); }

// ============================================================
//  Smoke Test (remove for production)
// ============================================================

int main() {
    const size_t payloadSize = 8;
    const size_t totalSize   = sizeof(MessageHeader) + payloadSize;

    std::vector<uint8_t> buf(totalSize, 0);

    // Build a fake header
    MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buf.data());
    hdr->magic         = 0xDEADBEEF;
    hdr->version       = 1;
    hdr->flags         = 0x0001;
    hdr->record_count  = 2;
    hdr->record_size   = 4;
    hdr->payload_bytes = payloadSize;

    // Fill payload with recognizable bytes
    for (size_t i = 0; i < payloadSize; ++i)
        buf[sizeof(MessageHeader) + i] = static_cast<uint8_t>(0xAA + i);

    // Simulate the library firing the callback
    writeSomething(static_cast<int>(MessageType::Event),      nullptr, buf.data(), totalSize);
    writeSomething(static_cast<int>(MessageType::Event),      nullptr, buf.data(), totalSize);
    writeSomething(static_cast<int>(MessageType::Telemetry),  nullptr, buf.data(), totalSize);
    writeSomething(static_cast<int>(MessageType::Diagnostic), nullptr, buf.data(), totalSize);
    writeSomething(99, nullptr, buf.data(), totalSize); // Unknown — dropped

    printStoreSummary();

    auto events = getMessages(MessageType::Event);
    std::cout << "\nEvent messages: " << events.size() << "\n";
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& msg = events[i];
        std::cout << "  [" << i << "] magic=0x" << std::hex << msg.header.magic
                  << "  payload=" << std::dec << msg.payload.size() << " bytes\n";
    }

    return 0;
}
