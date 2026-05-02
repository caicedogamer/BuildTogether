#include <EditorP2P/core/Permissions.hpp>
#include <EditorP2P/core/EditorOperation.hpp>
#include <EditorP2P/config/BuildConfig.hpp>
#include <EditorP2P/editor/ObjectLockManager.hpp>
#include <EditorP2P/editor/ObjectRegistry.hpp>
#include <EditorP2P/net/Endpoint.hpp>
#include <EditorP2P/net/JoinCode.hpp>
#include <EditorP2P/protocol/GroupMetadata.hpp>
#include <EditorP2P/protocol/MessageCodec.hpp>
#include <EditorP2P/util/StringUtil.hpp>

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using namespace ep2p;

namespace {
    void testEndpointAndJoinCode() {
        auto endpoint = Endpoint::fromString("192.168.1.25:43720");
        assert(endpoint.host == "192.168.1.25");
        assert(endpoint.port == 43720);
        assert(Endpoint::fromString(":43720").empty());
        assert(Endpoint::fromString("192.168.1.25:notaport").empty());

        auto joinCode = JoinCode::parse("192.168.1.25:43720#abcd-1234");
        assert(joinCode);
        assert(joinCode->sessionKey == "ABCD-1234");
        assert(!JoinCode::parse("192.168.1.25:43720#AB|D-1234"));
    }

    void testPermissions() {
        auto viewer = permissionsForRole(Role::Viewer);
        assert(!viewer.canEdit);

        auto builder = permissionsForRole(Role::Builder);
        assert(builder.canEdit);
        assert(builder.canDelete);
        assert(!builder.canSave);

        auto roundTrip = PermissionFlags::fromBits(builder.toBits());
        assert(roundTrip.canEdit);
        assert(roundTrip.canDelete);
        assert(!roundTrip.canSave);
    }

    void testMessageCodec() {
        Message msg;
        msg.type = MessageType::Hello;
        msg.senderId = 7;
        msg.sequence = 42;
        msg.payload = "1|ABCD-1234|Player|1";

        auto bytes = MessageCodec::encode(msg);
        std::vector<Message> decoded;
        MessageCodec codec;
        codec.feed(bytes.data(), bytes.size(), [&](Message parsed) {
            decoded.push_back(parsed);
        });

        assert(decoded.size() == 1);
        assert(decoded.front().type == MessageType::Hello);
        assert(decoded.front().senderId == 7);
        assert(decoded.front().sequence == 42);
        assert(decoded.front().payload == msg.payload);

        MessageCodec::PlaceObjectFields fields;
        fields.tempClientId = 12;
        fields.gdObjectId = 901;
        fields.x = 30.f;
        fields.y = 60.f;
        fields.rotation = 0.f;
        fields.scaleX = 1.f;
        fields.scaleY = 1.f;
        fields.saveString = "1,901,2,30,3,60,51,123|kept";

        auto payload = MessageCodec::encodePlaceObject(fields);
        auto parsed = MessageCodec::decodePlaceObject(payload);
        assert(parsed);
        assert(parsed.value().saveString == fields.saveString);
    }

    void testSnapshotCodec() {
        // Single-chunk round-trip with a save string containing '|'.
        MessageCodec::SnapshotChunkFields f;
        f.chunkIndex  = 0;
        f.totalChunks = 2;
        f.objects.push_back({1, "1,901,2,30,3,60"});
        f.objects.push_back({2, "1,1,2,100,3,200,51,abc|def"});

        auto payload = MessageCodec::encodeSnapshotChunk(f);
        auto parsed  = MessageCodec::decodeSnapshotChunk(payload);
        assert(parsed);
        assert(parsed.value().chunkIndex  == 0);
        assert(parsed.value().totalChunks == 2);
        assert(parsed.value().objects.size() == 2);
        assert(parsed.value().objects[0].netId      == 1);
        assert(parsed.value().objects[0].saveString == "1,901,2,30,3,60");
        assert(parsed.value().objects[1].netId      == 2);
        assert(parsed.value().objects[1].saveString == "1,1,2,100,3,200,51,abc|def");

        // Empty chunk (valid for last chunk when object count is a multiple of budget).
        MessageCodec::SnapshotChunkFields empty;
        empty.chunkIndex  = 1;
        empty.totalChunks = 2;
        auto emptyPayload = MessageCodec::encodeSnapshotChunk(empty);
        auto emptyParsed  = MessageCodec::decodeSnapshotChunk(emptyPayload);
        assert(emptyParsed);
        assert(emptyParsed.value().objects.empty());

        // Bad payload.
        assert(!MessageCodec::decodeSnapshotChunk("nopipes"));
        assert(!MessageCodec::decodeSnapshotChunk("0|"));
    }

    void testRegistries() {
        int objectA = 1;
        int objectB = 2;

        auto& registry = ObjectRegistry::get();
        registry.clear();
        auto idA = registry.registerObject(&objectA);
        assert(idA != INVALID_OBJECT_ID);
        assert(registry.findByNetworkId(idA) == &objectA);
        ObjectRegistry::ObjectFingerprint fpA;
        fpA.objectId = 1;
        fpA.x = 10.f;
        fpA.y = 20.f;
        fpA.rotation = 0.f;
        fpA.scaleX = 1.f;
        fpA.scaleY = 1.f;
        fpA.saveString = "1,1,2,10,3,20";
        registry.updateFingerprint(idA, fpA);
        registry.bindObject(42, &objectB);
        assert(registry.findNetworkId(&objectB) == 42);
        ObjectRegistry::ObjectFingerprint fpB;
        fpB.objectId = 1;
        fpB.x = 40.f;
        fpB.y = 60.f;
        fpB.rotation = 90.f;
        fpB.scaleX = 2.f;
        fpB.scaleY = 1.5f;
        fpB.saveString = "1,1,2,40,3,60,6,90,128,unique";
        registry.updateFingerprint(42, fpB);

        ObjectRegistry::MatchStats stats;
        assert(registry.findLikelyNetworkId(fpB, &stats) == 42);
        assert(stats.registryCount == 2);
        assert(stats.exactSaveStringMatches == 1);

        auto staleDeleteObject = fpB;
        staleDeleteObject.saveString.clear();
        assert(registry.findLikelyNetworkId(staleDeleteObject) == 42);

        ObjectRegistry::ObjectFingerprint ambiguous = fpA;
        ambiguous.saveString.clear();
        registry.bindObject(43, &objectB);
        registry.updateFingerprint(43, ambiguous);
        assert(registry.findLikelyNetworkId(ambiguous) == INVALID_OBJECT_ID);
        registry.clear();
        assert(registry.count() == 0);

        auto& locks = ObjectLockManager::get();
        locks.clear();
        LockDenyReason reason = LockDenyReason::AlreadyLocked;
        assert(locks.tryLock(42, 1, reason));
        assert(locks.isLocked(42));
        assert(!locks.tryLock(42, 2, reason));
        locks.unlockAllForPlayer(1);
        assert(!locks.isLocked(42));
    }

    void testGroupMetadataParser() {
        auto metadata = GroupMetadataParser::parseSaveString(
            "1,901,2,30,3,60,57,12.34.56,51,77,999,abc"
        );
        assert(metadata.objectGroups.size() == 3);
        assert(metadata.objectGroups[0] == 12);
        assert(metadata.objectGroups[1] == 34);
        assert(metadata.objectGroups[2] == 56);
        assert(metadata.targetGroups.size() == 1);
        assert(metadata.targetGroups[0] == 77);
        assert(metadata.fields.size() == 2);
        assert(GroupMetadataParser::summarize(metadata).find("objectGroups=[12,34,56]") != std::string::npos);

        auto empty = GroupMetadataParser::parseSaveString("1,1,2,30,3,60");
        assert(empty.empty());
    }

    void testEditorOperationRecorder() {
        auto& recorder = EditorOperationRecorder::get();
        recorder.clear();

        EditorOperation operation;
        operation.type = EditorOperationType::Place;
        operation.direction = EditorOperationDirection::LocalSend;
        operation.netId = 42;
        operation.gdObjectId = 1;
        operation.x = 90.f;
        operation.y = 120.f;
        operation.origin = "unit";
        operation.saveString = "1,1,2,90,3,120,57,5";
        operation.groups = GroupMetadataParser::parseSaveString(operation.saveString);

        auto id = recorder.record(operation);
        auto snapshot = recorder.snapshot();
        assert(id == 1);
        assert(snapshot.size() == 1);
        assert(snapshot[0].id == 1);
        assert(snapshot[0].netId == 42);
        assert(snapshot[0].groups.objectGroups.size() == 1);
        assert(snapshot[0].groups.objectGroups[0] == 5);

        recorder.clear();
        assert(recorder.snapshot().empty());
    }
}

int main() {
    testEndpointAndJoinCode();
    testPermissions();
    testMessageCodec();
    testSnapshotCodec();
    testRegistries();
    testGroupMetadataParser();
    testEditorOperationRecorder();
    return 0;
}
