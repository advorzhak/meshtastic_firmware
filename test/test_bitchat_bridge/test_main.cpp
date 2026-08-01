#include "TestUtil.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/BitChatBridgeModule.h"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unity.h>

#if !MESHTASTIC_EXCLUDE_BITCHAT_BRIDGE

static constexpr size_t TEST_MAX_BROADCAST_PAYLOAD = 207;
static constexpr size_t TEST_MAX_RECIPIENT_PAYLOAD = 199;
static constexpr size_t TEST_MAX_SIGNATURE_PAYLOAD = 143;
static constexpr size_t TEST_MAX_RECIPIENT_SIGNATURE_PAYLOAD = 135;

class BitChatBridgeModuleTestShim : public BitChatBridgeModule
{
  public:
    uint64_t calculateHashForTest(const BitChatMessage &msg) const { return calculateHash(msg); }
    bool isDuplicateForTest(const BitChatMessage &msg) { return isDuplicate(msg); }
    void rememberForTest(const BitChatMessage &msg) { remember(msg); }
    size_t serializedBitChatLengthForTest(const BitChatMessage &msg) const { return serializedBitChatLength(msg); }
    bool canRelayToMeshForTest(const BitChatMessage &msg) const { return canRelayToMesh(msg); }
    meshtastic_MeshPacket *createMeshPacketForTest(const BitChatMessage &msg) const { return createMeshPacket(msg); }
    size_t maxMeshFrameSizeForTest() const { return BITCHAT_MAX_MESH_FRAME_SIZE; }
};

class MockNodeDB : public NodeDB
{
};

class MockRouter : public Router
{
  public:
    ~MockRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }
};

static std::unique_ptr<MockNodeDB> mockNodeDB;
static std::unique_ptr<MockRouter> mockRouter;

static BitChatMessage makeTestMessage()
{
    BitChatMessage msg;
    msg.version = 1;
    msg.type = BITCHAT_MSG_MESSAGE;
    msg.ttl = 7;
    msg.timestamp = 0x0102030405060708ULL;
    msg.flags = 0;
    msg.setSenderId32(0x12345678);

    const char *payload = "hello";
    msg.payloadLength = strlen(payload);
    memcpy(msg.payload.data(), payload, msg.payloadLength);
    return msg;
}

static BitChatMessage makeSizedMessage(uint8_t flags, size_t payloadLength)
{
    BitChatMessage msg = makeTestMessage();
    msg.flags = flags;
    msg.payloadLength = payloadLength;
    msg.payload.fill(0x42);
    msg.setRecipientId32(0x87654321);
    msg.signature.fill(0xAA);
    return msg;
}

static void assertMeshRelayBoundary(uint8_t flags, size_t maxPayloadLength)
{
    auto module = std::make_unique<BitChatBridgeModuleTestShim>();
    BitChatMessage fits = makeSizedMessage(flags, maxPayloadLength);
    BitChatMessage tooLarge = makeSizedMessage(flags, maxPayloadLength + 1);
    const uint32_t maxFrame = static_cast<uint32_t>(module->maxMeshFrameSizeForTest());

    TEST_ASSERT_TRUE(module->canRelayToMeshForTest(fits));
    TEST_ASSERT_EQUAL_UINT32(maxFrame, static_cast<uint32_t>(module->serializedBitChatLengthForTest(fits)));

    TEST_ASSERT_FALSE(module->canRelayToMeshForTest(tooLarge));
    TEST_ASSERT_EQUAL_UINT32(maxFrame + 1, static_cast<uint32_t>(module->serializedBitChatLengthForTest(tooLarge)));
}

static void test_hash_is_stable_across_ttl_changes()
{
    auto module = std::make_unique<BitChatBridgeModuleTestShim>();
    BitChatMessage original = makeTestMessage();
    BitChatMessage relayed = original;
    relayed.ttl = 2;

    TEST_ASSERT_EQUAL_HEX32(module->calculateHashForTest(original), module->calculateHashForTest(relayed));

    module->rememberForTest(original);
    TEST_ASSERT_TRUE(module->isDuplicateForTest(relayed));
}

static void test_hash_changes_when_message_identity_changes()
{
    auto module = std::make_unique<BitChatBridgeModuleTestShim>();
    BitChatMessage original = makeTestMessage();
    BitChatMessage changed = original;
    changed.payload[0] = 'H';

    TEST_ASSERT_NOT_EQUAL(module->calculateHashForTest(original), module->calculateHashForTest(changed));
}

static void test_hash_changes_with_recipient_for_direct_messages()
{
    auto module = std::make_unique<BitChatBridgeModuleTestShim>();
    BitChatMessage original = makeTestMessage();
    original.flags = BITCHAT_FLAG_HAS_RECIPIENT;
    original.setRecipientId32(0x11111111);

    BitChatMessage changed = original;
    changed.setRecipientId32(0x22222222);

    TEST_ASSERT_NOT_EQUAL(module->calculateHashForTest(original), module->calculateHashForTest(changed));
}

static void test_hash_ignores_recipient_when_flag_not_set()
{
    auto module = std::make_unique<BitChatBridgeModuleTestShim>();
    BitChatMessage original = makeTestMessage();
    original.flags = 0;
    original.setRecipientId32(0x11111111);

    BitChatMessage changed = original;
    changed.setRecipientId32(0x22222222);

    TEST_ASSERT_EQUAL_HEX32(module->calculateHashForTest(original), module->calculateHashForTest(changed));
}

static void test_hash_changes_with_signature_when_present()
{
    auto module = std::make_unique<BitChatBridgeModuleTestShim>();
    BitChatMessage original = makeTestMessage();
    original.flags = BITCHAT_FLAG_HAS_SIGNATURE;
    original.signature[0] = 0xAA;

    BitChatMessage changed = original;
    changed.signature[0] = 0x55;

    TEST_ASSERT_NOT_EQUAL(module->calculateHashForTest(original), module->calculateHashForTest(changed));
}

static void test_mesh_relay_payload_limits()
{
    assertMeshRelayBoundary(0, TEST_MAX_BROADCAST_PAYLOAD);
    assertMeshRelayBoundary(BITCHAT_FLAG_HAS_RECIPIENT, TEST_MAX_RECIPIENT_PAYLOAD);
    assertMeshRelayBoundary(BITCHAT_FLAG_HAS_SIGNATURE, TEST_MAX_SIGNATURE_PAYLOAD);
    assertMeshRelayBoundary(BITCHAT_FLAG_HAS_RECIPIENT | BITCHAT_FLAG_HAS_SIGNATURE, TEST_MAX_RECIPIENT_SIGNATURE_PAYLOAD);
}

static void test_mesh_packet_hop_limit_is_capped_by_lora_config()
{
    auto module = std::make_unique<BitChatBridgeModuleTestShim>();
    BitChatMessage msg = makeTestMessage();
    msg.ttl = 6;
    config.lora.hop_limit = 2;

    meshtastic_MeshPacket *packet = module->createMeshPacketForTest(msg);
    TEST_ASSERT_NOT_NULL(packet);
    TEST_ASSERT_EQUAL_UINT8(2, packet->hop_limit);
    packetPool.release(packet);
}

static void test_mesh_packet_hop_limit_keeps_lower_bitchat_ttl()
{
    auto module = std::make_unique<BitChatBridgeModuleTestShim>();
    BitChatMessage msg = makeTestMessage();
    msg.ttl = 2;
    config.lora.hop_limit = 5;

    meshtastic_MeshPacket *packet = module->createMeshPacketForTest(msg);
    TEST_ASSERT_NOT_NULL(packet);
    TEST_ASSERT_EQUAL_UINT8(2, packet->hop_limit);
    packetPool.release(packet);
}

static void test_mesh_packet_preserves_single_hop_ttl()
{
    auto module = std::make_unique<BitChatBridgeModuleTestShim>();
    BitChatMessage msg = makeTestMessage();
    msg.ttl = 1;
    config.lora.hop_limit = 5;

    meshtastic_MeshPacket *packet = module->createMeshPacketForTest(msg);
    TEST_ASSERT_NOT_NULL(packet);
    TEST_ASSERT_EQUAL_UINT8(1, packet->hop_limit);
    packetPool.release(packet);
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    mockNodeDB = std::make_unique<MockNodeDB>();
    mockRouter = std::make_unique<MockRouter>();
    nodeDB = mockNodeDB.get();
    router = mockRouter.get();
    myNodeInfo.my_node_num = 0x12345678;

    UNITY_BEGIN();
    RUN_TEST(test_hash_is_stable_across_ttl_changes);
    RUN_TEST(test_hash_changes_when_message_identity_changes);
    RUN_TEST(test_hash_changes_with_recipient_for_direct_messages);
    RUN_TEST(test_hash_ignores_recipient_when_flag_not_set);
    RUN_TEST(test_hash_changes_with_signature_when_present);
    RUN_TEST(test_mesh_relay_payload_limits);
    RUN_TEST(test_mesh_packet_hop_limit_is_capped_by_lora_config);
    RUN_TEST(test_mesh_packet_hop_limit_keeps_lower_bitchat_ttl);
    RUN_TEST(test_mesh_packet_preserves_single_hop_ttl);
    exit(UNITY_END());
}

void loop() {}

#else

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

void loop() {}

#endif
