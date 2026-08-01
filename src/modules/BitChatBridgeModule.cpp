#include "BitChatBridgeModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "mesh/Utf8Helpers.h"
#include <algorithm>
#include <cstring>

#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
#include "nimble/NimbleBluetooth.h"
extern BLEServer *bleServer;
extern NimbleBluetooth *nimbleBluetooth;
#endif

#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
class BitChatBridgeModule::BitChatCharacteristicCallbacks : public BLECharacteristicCallbacks
{
  public:
    explicit BitChatCharacteristicCallbacks(BitChatBridgeModule *module) : module(module) {}

    virtual void onWrite(BLECharacteristic *characteristic) override
    {
        module->handleBleWrite(characteristic->getData(), characteristic->getLength());
    }

  private:
    BitChatBridgeModule *module;
};
#endif

BitChatBridgeModule::BitChatBridgeModule()
    : SinglePortModule("bitchat", meshtastic_PortNum_PRIVATE_APP), concurrency::OSThread("BitChatBridge")
{
}

void BitChatBridgeModule::setup()
{
    setIntervalFromNow(setStartDelay());
}

int32_t BitChatBridgeModule::runOnce()
{
#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
    processBleQueue();
#endif

    if (peerId == 0) {
        peerId = nodeDB->getNodeNum();
    }

    setupBleService();

    if (bleServiceReady) {
        uint32_t now = millis();
        if ((now - lastAnnounceMs) >= ANNOUNCE_INTERVAL_MS) {
            sendPeerAnnouncement();
            lastAnnounceMs = now;
        }
    }

#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
    {
        std::lock_guard<std::mutex> lock(bleQueueMutex);
        if (bleQueueCount > 0) {
            return 100;
        }
    }
#endif

    return 5000;
}

ProcessMessage BitChatBridgeModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    BitChatMessage msg;
    if (!extractBitChatMessage(mp, msg)) {
        return ProcessMessage::CONTINUE;
    }

    if (isDuplicate(msg)) {
        LOG_DEBUG("BitChat: Duplicate mesh packet dropped");
        return ProcessMessage::CONTINUE;
    }
    if (broadcastToBle(msg)) {
        remember(msg);
    }
    return ProcessMessage::CONTINUE;
}

bool BitChatBridgeModule::setupBleService()
{
#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
    if (bleServiceReady && bleAdvertisingReady) {
        return true;
    }
    if (!config.bluetooth.enabled || !bleServer) {
        return false;
    }

    auto restartAdvertising = [this]() -> bool {
        if (!nimbleBluetooth) {
            return false;
        }
        nimbleBluetooth->startAdvertising();
        return true;
    };

    if (!bitchatService) {
        bitchatService = bleServer->createService(BLEUUID(BITCHAT_SERVICE_UUID));
    }
    if (!bitchatService) {
        LOG_WARN("BitChat: Failed to create BLE service");
        restartAdvertising();
        return false;
    }

    uint32_t characteristicProperties = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
                                        BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY;
    if (config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN) {
        characteristicProperties |= BLECharacteristic::PROPERTY_READ_AUTHEN | BLECharacteristic::PROPERTY_READ_ENC |
                                    BLECharacteristic::PROPERTY_WRITE_AUTHEN | BLECharacteristic::PROPERTY_WRITE_ENC;
    }

    if (!bitchatCharacteristic) {
        bitchatCharacteristic =
            bitchatService->createCharacteristic(BLEUUID(BITCHAT_CHARACTERISTIC_UUID), characteristicProperties);
    }
    if (!bitchatCharacteristic) {
        LOG_WARN("BitChat: Failed to create BLE characteristic");
        restartAdvertising();
        return false;
    }

    if (!callbacks) {
        callbacks = std::make_unique<BitChatCharacteristicCallbacks>(this);
    }
    bitchatCharacteristic->setCallbacks(callbacks.get());
    if (!bleServiceStarted) {
        if (!bitchatService->start()) {
            LOG_WARN("BitChat: Failed to start BLE service");
            restartAdvertising();
            return false;
        }
        bleServiceStarted = true;
    }

    bleServiceReady = true;
    if (!restartAdvertising()) {
        LOG_WARN("BitChat: Failed to start BLE advertising");
        bleAdvertisingReady = false;
        return false;
    }
    bleAdvertisingReady = true;

    LOG_INFO("BitChat: BLE service ready");
    sendPeerAnnouncement();
    lastAnnounceMs = millis();
    return true;
#else
    return false;
#endif
}

void BitChatBridgeModule::handleBleWrite(const uint8_t *data, size_t length)
{
    BitChatMessage msg;
    if (!parseBitChatMessage(data, length, msg)) {
        LOG_DEBUG("BitChat: Ignoring invalid BLE write");
        return;
    }
    if (!canRelayToMesh(msg)) {
        LOG_WARN("BitChat: Ignoring BLE write too large for mesh relay (%u > %u)",
                 static_cast<unsigned>(serializedBitChatLength(msg)), static_cast<unsigned>(BITCHAT_MAX_MESH_FRAME_SIZE));
        return;
    }

#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
    {
        std::lock_guard<std::mutex> lock(bleQueueMutex);
        if (bleQueueCount == bleQueue.size()) {
            bleQueueHead = (bleQueueHead + 1) % bleQueue.size();
            bleQueueCount--;
            LOG_WARN("BitChat: BLE queue full, dropping oldest message");
        }

        const size_t insertIndex = (bleQueueHead + bleQueueCount) % bleQueue.size();
        bleQueue[insertIndex] = msg;
        bleQueueCount++;
    }
    setIntervalFromNow(0);
    concurrency::mainDelay.interrupt();
#else
    (void)msg;
#endif
}

void BitChatBridgeModule::processBleQueue()
{
#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
    while (true) {
        BitChatMessage msg;
        {
            std::lock_guard<std::mutex> lock(bleQueueMutex);
            if (bleQueueCount == 0) {
                break;
            }
            msg = bleQueue[bleQueueHead];
            bleQueueHead = (bleQueueHead + 1) % bleQueue.size();
            bleQueueCount--;
        }

        const uint64_t hash = calculateHash(msg);
        if (isDuplicate(hash)) {
            continue;
        }
        if (relayToMesh(msg)) {
            remember(hash);
        }
    }
#endif
}

bool BitChatBridgeModule::relayToMesh(BitChatMessage msg)
{
    if (msg.ttl > BITCHAT_MAX_HOPS) {
        msg.ttl = BITCHAT_MAX_HOPS;
    }
    if (msg.ttl == 0) {
        return false;
    }

    meshtastic_MeshPacket *packet = createMeshPacket(msg);
    if (!packet) {
        return false;
    }

    service->sendToMesh(packet, RX_SRC_LOCAL, true);
    return true;
}

bool BitChatBridgeModule::broadcastToBle(const BitChatMessage &msg)
{
#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
    if (!bleServiceReady || !bitchatCharacteristic) {
        return false;
    }

    uint8_t buffer[BITCHAT_MAX_BLE_FRAME_SIZE];
    size_t size = serializeBitChatMessage(msg, buffer, sizeof(buffer));
    if (size == 0) {
        return false;
    }

    bitchatCharacteristic->setValue(buffer, size);
    bitchatCharacteristic->notify();
    return true;
#else
    (void)msg;
    return false;
#endif
}

void BitChatBridgeModule::sendPeerAnnouncement()
{
    if (peerId == 0) {
        return;
    }
    broadcastToBle(createPeerAnnouncement());
}

BitChatMessage BitChatBridgeModule::createPeerAnnouncement()
{
    BitChatMessage msg;
    msg.type = BITCHAT_MSG_ANNOUNCE;
    msg.ttl = 7;
    msg.timestamp = static_cast<uint64_t>(getTime()) * 1000ULL;
    msg.setSenderId32(peerId);

    const char *name = owner.long_name;
    if (name[0] == '\0') {
        name = "Meshtastic";
    }

    static constexpr const char *namePrefix = "Meshtastic: ";
    static constexpr size_t prefixLength = sizeof(namePrefix) - 1;
    static constexpr size_t maxNicknameBytes = BITCHAT_MAX_MESH_FRAME_SIZE - BITCHAT_FIXED_HEADER_SIZE - BITCHAT_SENDER_SIZE - 2;

    const size_t availableForName = (maxNicknameBytes > prefixLength) ? (maxNicknameBytes - prefixLength) : 0;
    const size_t safeNameBytes = utf8SafePrefixLength(name, availableForName);
    const size_t nicknameLength = std::min(prefixLength + safeNameBytes, maxNicknameBytes);

    msg.payload[0] = 0x01;
    msg.payload[1] = static_cast<uint8_t>(nicknameLength);
    if (nicknameLength > 0) {
        const size_t prefixBytes = std::min(prefixLength, nicknameLength);
        memcpy(msg.payload.data() + 2, namePrefix, prefixBytes);
        if (nicknameLength > prefixBytes && safeNameBytes > 0) {
            memcpy(msg.payload.data() + 2 + prefixBytes, name, std::min(safeNameBytes, nicknameLength - prefixBytes));
        }
    }
    msg.payloadLength = static_cast<uint16_t>(2 + nicknameLength);
    return msg;
}

bool BitChatBridgeModule::parseBitChatMessage(const uint8_t *data, size_t length, BitChatMessage &msg) const
{
    if (!data || length < (BITCHAT_FIXED_HEADER_SIZE + BITCHAT_SENDER_SIZE)) {
        return false;
    }

    size_t offset = 0;
    msg = BitChatMessage();

    msg.version = data[offset++];
    if (msg.version != BITCHAT_VERSION) {
        return false;
    }
    msg.type = data[offset++];
    msg.ttl = data[offset++];

    msg.timestamp = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        msg.timestamp = (msg.timestamp << 8) | static_cast<uint64_t>(data[offset++]);
    }

    msg.flags = data[offset++];
    msg.payloadLength = (static_cast<uint16_t>(data[offset]) << 8) | static_cast<uint16_t>(data[offset + 1]);
    offset += 2;

    memcpy(msg.senderId.data(), data + offset, BITCHAT_SENDER_SIZE);
    offset += BITCHAT_SENDER_SIZE;

    const bool hasRecipient = (msg.flags & BITCHAT_FLAG_HAS_RECIPIENT) != 0;
    const bool hasSignature = (msg.flags & BITCHAT_FLAG_HAS_SIGNATURE) != 0;

    if (hasRecipient) {
        if (length < (offset + BITCHAT_SENDER_SIZE)) {
            return false;
        }
        memcpy(msg.recipientId.data(), data + offset, BITCHAT_SENDER_SIZE);
        offset += BITCHAT_SENDER_SIZE;
    }

    if (msg.payloadLength > msg.payload.size()) {
        return false;
    }

    const size_t requiredLength = offset + msg.payloadLength + (hasSignature ? BITCHAT_SIGNATURE_SIZE : 0);
    if (length < requiredLength) {
        return false;
    }

    if (msg.payloadLength > 0) {
        memcpy(msg.payload.data(), data + offset, msg.payloadLength);
        offset += msg.payloadLength;
    }

    if (hasSignature) {
        memcpy(msg.signature.data(), data + offset, BITCHAT_SIGNATURE_SIZE);
    }

    return true;
}

size_t BitChatBridgeModule::serializedBitChatLength(const BitChatMessage &msg) const
{
    if (msg.payloadLength > msg.payload.size()) {
        return 0;
    }

    const bool hasRecipient = (msg.flags & BITCHAT_FLAG_HAS_RECIPIENT) != 0;
    const bool hasSignature = (msg.flags & BITCHAT_FLAG_HAS_SIGNATURE) != 0;

    return BITCHAT_FIXED_HEADER_SIZE + BITCHAT_SENDER_SIZE + (hasRecipient ? BITCHAT_SENDER_SIZE : 0) + msg.payloadLength +
           (hasSignature ? BITCHAT_SIGNATURE_SIZE : 0);
}

bool BitChatBridgeModule::canRelayToMesh(const BitChatMessage &msg) const
{
    const size_t requiredLength = serializedBitChatLength(msg);
    return requiredLength != 0 && requiredLength <= BITCHAT_MAX_MESH_FRAME_SIZE;
}

size_t BitChatBridgeModule::serializeBitChatMessage(const BitChatMessage &msg, uint8_t *buffer, size_t maxLength) const
{
    if (!buffer) {
        return 0;
    }

    const size_t requiredLength = serializedBitChatLength(msg);
    if (requiredLength == 0) {
        return 0;
    }
    if (requiredLength > maxLength) {
        return 0;
    }

    const bool hasRecipient = (msg.flags & BITCHAT_FLAG_HAS_RECIPIENT) != 0;
    const bool hasSignature = (msg.flags & BITCHAT_FLAG_HAS_SIGNATURE) != 0;

    size_t offset = 0;
    buffer[offset++] = msg.version;
    buffer[offset++] = msg.type;
    buffer[offset++] = msg.ttl;

    for (int i = 7; i >= 0; --i) {
        buffer[offset++] = static_cast<uint8_t>((msg.timestamp >> (i * 8)) & 0xFF);
    }

    buffer[offset++] = msg.flags;
    buffer[offset++] = static_cast<uint8_t>((msg.payloadLength >> 8) & 0xFF);
    buffer[offset++] = static_cast<uint8_t>(msg.payloadLength & 0xFF);

    memcpy(buffer + offset, msg.senderId.data(), BITCHAT_SENDER_SIZE);
    offset += BITCHAT_SENDER_SIZE;

    if (hasRecipient) {
        memcpy(buffer + offset, msg.recipientId.data(), BITCHAT_SENDER_SIZE);
        offset += BITCHAT_SENDER_SIZE;
    }

    if (msg.payloadLength > 0) {
        memcpy(buffer + offset, msg.payload.data(), msg.payloadLength);
        offset += msg.payloadLength;
    }

    if (hasSignature) {
        memcpy(buffer + offset, msg.signature.data(), BITCHAT_SIGNATURE_SIZE);
        offset += BITCHAT_SIGNATURE_SIZE;
    }

    return offset;
}

bool BitChatBridgeModule::extractBitChatMessage(const meshtastic_MeshPacket &meshPacket, BitChatMessage &msg) const
{
    static constexpr uint8_t magic[BITCHAT_MESH_MAGIC_SIZE] = {'B', 'C', 'H', 'T'};

    if (meshPacket.decoded.portnum != meshtastic_PortNum_PRIVATE_APP ||
        meshPacket.decoded.payload.size < (BITCHAT_MESH_MAGIC_SIZE + BITCHAT_FIXED_HEADER_SIZE + BITCHAT_SENDER_SIZE)) {
        return false;
    }

    if (memcmp(meshPacket.decoded.payload.bytes, magic, BITCHAT_MESH_MAGIC_SIZE) != 0) {
        return false;
    }

    return parseBitChatMessage(meshPacket.decoded.payload.bytes + BITCHAT_MESH_MAGIC_SIZE,
                               meshPacket.decoded.payload.size - BITCHAT_MESH_MAGIC_SIZE, msg);
}

meshtastic_MeshPacket *BitChatBridgeModule::createMeshPacket(const BitChatMessage &msg) const
{
    static constexpr uint8_t magic[BITCHAT_MESH_MAGIC_SIZE] = {'B', 'C', 'H', 'T'};

    if (!canRelayToMesh(msg)) {
        LOG_WARN("BitChat: Message too large for mesh packet (%u > %u)", static_cast<unsigned>(serializedBitChatLength(msg)),
                 static_cast<unsigned>(BITCHAT_MAX_MESH_FRAME_SIZE));
        return nullptr;
    }

    meshtastic_MeshPacket *packet = router->allocForSending();
    if (!packet) {
        return nullptr;
    }
    packet->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;

    memcpy(packet->decoded.payload.bytes, magic, BITCHAT_MESH_MAGIC_SIZE);
    const size_t encodedSize = serializeBitChatMessage(msg, packet->decoded.payload.bytes + BITCHAT_MESH_MAGIC_SIZE,
                                                       sizeof(packet->decoded.payload.bytes) - BITCHAT_MESH_MAGIC_SIZE);
    if (encodedSize == 0) {
        packetPool.release(packet);
        return nullptr;
    }

    packet->decoded.payload.size = BITCHAT_MESH_MAGIC_SIZE + encodedSize;
    packet->hop_limit = std::min(packet->hop_limit, msg.ttl);
    packet->want_ack = false;
    return packet;
}

#if !MESHTASTIC_EXCLUDE_BLUETOOTH && defined(ARCH_ESP32)
static constexpr uint64_t FNV1_64_INIT = 0xcbf29ce484222325ULL;
static constexpr uint64_t FNV1_64_PRIME = 0x00000100000001b3ULL;

uint64_t BitChatBridgeModule::calculateHash(const BitChatMessage &msg) const
{
    uint64_t hash = FNV1_64_INIT;
    auto mix = [&hash](uint8_t b) {
        hash ^= static_cast<uint64_t>(b);
        hash *= FNV1_64_PRIME;
    };

    mix(msg.version);
    mix(msg.type);
    mix(msg.flags);
    mix(static_cast<uint8_t>((msg.payloadLength >> 8) & 0xFF));
    mix(static_cast<uint8_t>(msg.payloadLength & 0xFF));
    for (int i = 0; i < 8; ++i) {
        mix(static_cast<uint8_t>((msg.timestamp >> (i * 8)) & 0xFF));
    }
    for (auto b : msg.senderId) {
        mix(b);
    }
    if ((msg.flags & BITCHAT_FLAG_HAS_RECIPIENT) != 0) {
        for (auto b : msg.recipientId) {
            mix(b);
        }
    }
    for (size_t i = 0; i < msg.payloadLength; ++i) {
        mix(msg.payload[i]);
    }
    if ((msg.flags & BITCHAT_FLAG_HAS_SIGNATURE) != 0) {
        for (auto b : msg.signature) {
            mix(b);
        }
    }

    return hash;
}

bool BitChatBridgeModule::isDuplicate(const BitChatMessage &msg)
{
    return isDuplicate(calculateHash(msg));
}

bool BitChatBridgeModule::isDuplicate(uint64_t hash) const
{
    std::lock_guard<std::mutex> lock(dedupMutex);
    for (uint64_t seen : duplicateHashes) {
        if (seen != 0 && seen == hash) {
            return true;
        }
    }
    return false;
}

void BitChatBridgeModule::remember(const BitChatMessage &msg)
{
    remember(calculateHash(msg));
}

void BitChatBridgeModule::remember(uint64_t hash)
{
    std::lock_guard<std::mutex> lock(dedupMutex);
    duplicateHashes[duplicateIndex] = hash;
    duplicateIndex = (duplicateIndex + 1) % duplicateHashes.size();
}
#endif
