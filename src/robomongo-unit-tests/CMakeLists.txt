set(ROBO_SRC_DIR "${CMAKE_SOURCE_DIR}/src/robomongo")
add_executable(robo_unit_tests
    ${ROBO_SRC_DIR}/utils/RoboCrypt_test.cpp
    ${ROBO_SRC_DIR}/utils/StringOperations_test.cpp
    ${ROBO_SRC_DIR}/core/HexUtils_test.cpp
    ${ROBO_SRC_DIR}/core/bson/Bson_test.cpp
    ${ROBO_SRC_DIR}/core/mongodb/MongoClient_test.cpp)
target_link_libraries(robo_unit_tests PRIVATE robo_core GTest::gtest_main)
add_test(NAME robo_unit_tests COMMAND robo_unit_tests)
add_executable(robo_mongodb_integration_tests EXCLUDE_FROM_ALL MongoClientIntegration_test.cpp)
target_link_libraries(robo_mongodb_integration_tests PRIVATE robo_core GTest::gtest)
target_sources(robo_unit_tests PRIVATE ${ROBO_SRC_DIR}/core/mongodb/MongoConnection_test.cpp)
add_executable(robo_shell_integration_tests EXCLUDE_FROM_ALL ${ROBO_SRC_DIR}/core/engine/ScriptEngine_integration_test.cpp)
target_link_libraries(robo_shell_integration_tests PRIVATE robo_core)
add_executable(robo_gui_tests OutputWidget_test.cpp)
target_link_libraries(robo_gui_tests PRIVATE robo_core GTest::gtest)
add_test(NAME robo_gui_tests COMMAND robo_gui_tests)
set_tests_properties(robo_gui_tests PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ROBOMONGO_PROFILE_DIR=${CMAKE_CURRENT_BINARY_DIR}/gui-test-profile")
