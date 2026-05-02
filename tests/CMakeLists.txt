add_executable(ep2p_unit_tests
    unit_smoke.cpp
    ../src/core/EditorOperation.cpp
    ../src/core/Permissions.cpp
    ../src/editor/ObjectLockManager.cpp
    ../src/editor/ObjectRegistry.cpp
    ../src/net/Endpoint.cpp
    ../src/net/JoinCode.cpp
    ../src/protocol/GroupMetadata.cpp
    ../src/protocol/MessageCodec.cpp
    ../src/util/StringUtil.cpp
)

target_include_directories(ep2p_unit_tests PRIVATE
    ../include
)

add_test(NAME ep2p_unit_tests COMMAND ep2p_unit_tests)
