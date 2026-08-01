#pragma once

#include <cstddef>

#if defined(round) || defined(abs)
#undef round
#undef abs
#endif

#include <array>
#include <memory>
#include <mutex>

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
#include <BLECharacteristic.h>
#include <BLEServer.h>
#include <BLEService.h>
#endif

#define BITCHAT_SERVICE_UUID "F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C"
#define BITCHAT_CHARACTERISTIC_UUID "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D"
static constexpr size_t BITCHAT_MAX_PAYLOAD_SIZE = 217;
static constexpr uint8_t BITCHAT_FLAG_HAS_RECIPIENT = 0x01;
static constexpr uint8_t BITCHAT_FLAG_HAS_SIGNATURE = 0x02;

enum BitChatMessageType : uint8_t {
    BITCHAT_MSG_ANNOUNCE = 0x01,
    BITCHAT_MSG_MESSAGE = 0x02,
    BITCHAT_MSG_LEAVE = 0x03,
    BITCHAT_MSG_IDENTITY = 0x04,
    BITCHAT_MSG_CHANNEL = 0x05,
    BITCHAT_MSG_PING = 0x06,
    BITCHAT_MSG_PONG = 0x07,
};

struct BitChatMessage {
    uint8_t version = 1;
    uint8_t type = 0;
    uint8_t ttl = 0;
    uint64_t timestamp = 0;
    uint8_t flags = 0;
    uint16_t payloadLength = 0;
    std::array<uint8_t, 8> senderId = {};
    std::array<uint8_t, 8> recipientId = {};
    std::array<uint8_t, BITCHAT_MAX_PAYLOAD_SIZE> payload = {};
    std::array<uint8_t, 64> signature = {};

    uint32_t getSenderId32() const
    {
        return static_cast<uint32_t>(senderId[0]) | (static_cast<uint32_t>(senderId[1]) << 8) |
               (static_cast<uint32_t>(senderId[2]) << 16) | (static_cast<uint32_t>(senderId[3]) << 24);
    }

    void setSenderId32(uint32_t id)
    {
        senderId.fill(0);
        senderId[0] = static_cast<uint8_t>(id & 0xFF);
        senderId[1] = static_cast<uint8_t>((id >> 8) & 0xFF);
        senderId[2] = static_cast<uint8_t>((id >> 16) & 0xFF);
        senderId[3] = static_cast<uint8_t>((id >> 24) & 0xFF);
    }

    void setRecipientId32(uint32_t id)
    {
        recipientId.fill(0);
        recipientId[0] = static_cast<uint8_t>(id & 0xFF);
        recipientId[1] = static_cast<uint8_t>((id >> 8) & 0xFF);
        recipientId[2] = static_cast<uint8_t>((id >> 16) & 0xFF);
        recipientId[3] = static_cast<uint8_t>((id >> 24) & 0xFF);
    }
};

class BitChatBridgeModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    BitChatBridgeModule();

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual int32_t runOnce() override;
    virtual void setup() override;

  private:
#ifdef UNIT_TEST
    friend class BitChatBridgeModuleTestShim;
#endif
    static constexpr uint8_t BITCHAT_VERSION = 1;
    static constexpr size_t BITCHAT_FIXED_HEADER_SIZE = 14;
    static constexpr size_t BITCHAT_SENDER_SIZE = 8;
    static constexpr size_t BITCHAT_SIGNATURE_SIZE = 64;
    static constexpr size_t BITCHAT_MAX_BLE_FRAME_SIZE =
        BITCHAT_FIXED_HEADER_SIZE + BITCHAT_SENDER_SIZE + BITCHAT_SENDER_SIZE + BITCHAT_MAX_PAYLOAD_SIZE + BITCHAT_SIGNATURE_SIZE;
    static constexpr size_t BITCHAT_MESH_MAGIC_SIZE = 4;
    static constexpr size_t BITCHAT_MESH_PAYLOAD_SIZE = member_size(meshtastic_Data_payload_t, bytes);
    static constexpr size_t BITCHAT_MAX_MESH_FRAME_SIZE = BITCHAT_MESH_PAYLOAD_SIZE - BITCHAT_MESH_MAGIC_SIZE;
    static constexpr uint8_t BITCHAT_MAX_HOPS = 8;
    static constexpr uint32_t ANNOUNCE_INTERVAL_MS = 30000;

    bool bleServiceReady = false;
    bool bleAdvertisingReady = false;
    bool bleServiceStarted = false;
    uint32_t peerId = 0;
    uint32_t lastAnnounceMs = 0;

#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
    static constexpr size_t DEDUP_TABLE_SIZE = 256;
    std::array<uint64_t, DEDUP_TABLE_SIZE> duplicateHashes = {};
    size_t duplicateIndex = 0;
    mutable std::mutex dedupMutex;

    class BitChatCharacteristicCallbacks;
    std::unique_ptr<BitChatCharacteristicCallbacks> callbacks;
    BLEService *bitchatService = nullptr;
    BLECharacteristic *bitchatCharacteristic = nullptr;
    std::array<BitChatMessage, 8> bleQueue = {};
    size_t bleQueueHead = 0;
    size_t bleQueueCount = 0;
    std::mutex bleQueueMutex;
#endif

    bool setupBleService();
    void handleBleWrite(const uint8_t *data, size_t length);
    void processBleQueue();
    bool broadcastToBle(const BitChatMessage &msg);
    bool relayToMesh(BitChatMessage msg);
    void sendPeerAnnouncement();
    BitChatMessage createPeerAnnouncement();

    bool parseBitChatMessage(const uint8_t *data, size_t length, BitChatMessage &msg) const;
    size_t serializedBitChatLength(const BitChatMessage &msg) const;
    bool canRelayToMesh(const BitChatMessage &msg) const;
    size_t serializeBitChatMessage(const BitChatMessage &msg, uint8_t *buffer, size_t maxLength) const;
    bool extractBitChatMessage(const meshtastic_MeshPacket &meshPacket, BitChatMessage &msg) const;
    meshtastic_MeshPacket *createMeshPacket(const BitChatMessage &msg) const;

    uint64_t calculateHash(const BitChatMessage &msg) const;
    bool isDuplicate(const BitChatMessage &msg);
    bool isDuplicate(uint64_t hash) const;
    void remember(const BitChatMessage &msg);
    void remember(uint64_t hash);
};

extern BitChatBridgeModule *bitchatBridgeModule;
