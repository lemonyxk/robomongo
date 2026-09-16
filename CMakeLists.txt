cmake_minimum_required(VERSION 3.22)
project(Robomongo VERSION 2.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.5 CACHE STRING "Minimum macOS version")
endif()

# Dependencies are installed locally by scripts/build-macos.sh. Every version is
# pinned, while custom prefixes remain available for other platforms/CI.
set(ROBOMONGO_DEPS_ROOT "${CMAKE_SOURCE_DIR}/build/deps" CACHE PATH "Dependency cache")
set(ROBOMONGO_MODERN_PREFIX "${ROBOMONGO_DEPS_ROOT}/modern/prefix" CACHE PATH "Native dependency prefix")
list(PREPEND CMAKE_PREFIX_PATH "${ROBOMONGO_MODERN_PREFIX}")
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "${ROBOMONGO_DEPS_ROOT}/Qt6/6.11.2/macos")
endif()
set(OPENSSL_ROOT_DIR "${ROBOMONGO_MODERN_PREFIX}")
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(Threads REQUIRED)
find_package(OpenSSL 4.0.2 REQUIRED)
find_package(Qt6 6.11.2 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Network Xml Svg)
find_package(bson 2.5.3 EXACT CONFIG REQUIRED)
find_package(mongoc 2.5.3 EXACT CONFIG REQUIRED)
find_package(Libssh2 1.11.1 CONFIG QUIET)
if(NOT TARGET libssh2::libssh2_static)
    find_package(libssh2 1.11.1 CONFIG REQUIRED)
endif()
set(QSCINTILLA_INCLUDE_DIR "${ROBOMONGO_DEPS_ROOT}/QScintilla_src-2.14.1/src" CACHE PATH "QScintilla headers")
set(QSCINTILLA_LIBRARY "${ROBOMONGO_DEPS_ROOT}/qscintilla-arm64/libqscintilla2_qt6.a" CACHE FILEPATH "QScintilla Qt 6 library")
add_library(qscintilla STATIC IMPORTED)
set_target_properties(qscintilla PROPERTIES IMPORTED_LOCATION "${QSCINTILLA_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${QSCINTILLA_INCLUDE_DIR}" INTERFACE_LINK_LIBRARIES "Qt6::Widgets;Qt6::PrintSupport")
set(ROBOMONGO_NODE_EXECUTABLE "${ROBOMONGO_DEPS_ROOT}/modern/node-v26.8.2-darwin-arm64/bin/node" CACHE FILEPATH "Node runtime")
set(ROBOMONGO_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/src/robomongo/runtime" CACHE PATH "mongosh bridge source")
set(ROBOMONGO_NODE_MODULES "${ROBOMONGO_DEPS_ROOT}/mongosh-runtime/node_modules" CACHE PATH "Installed mongosh packages")
if(NOT EXISTS "${ROBOMONGO_NODE_EXECUTABLE}" OR NOT EXISTS "${ROBOMONGO_NODE_MODULES}/@mongosh/shell-api/package.json")
    message(FATAL_ERROR "Modern JavaScript runtime missing. Run scripts/build-macos.sh first.")
endif()
add_library(robo_dependencies INTERFACE)
target_link_libraries(robo_dependencies INTERFACE Qt6::Widgets Qt6::PrintSupport Qt6::Network Qt6::Xml Qt6::Svg qscintilla mongoc::static OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_include_directories(robo_dependencies INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(robo_dependencies INTERFACE
    BUILDING_ROBO "PROJECT_NAME=\"Robo 3T\"" "PROJECT_NAME_TITLE=\"Robo 3T\""
    "PROJECT_COPYRIGHT=\"Copyright (C) 2014-2026 Robo 3T contributors\""
    "PROJECT_DOMAIN=\"www.robomongo.org\"" "PROJECT_COMPANYNAME=\"3T Software Labs Ltd\""
    "PROJECT_COMPANYNAME_DOMAIN=\"https://studio3t.com/\""
    "PROJECT_GITHUB_FORK=\"www.github.com/Studio3T/robomongo\""
    "PROJECT_GITHUB_ISSUES=\"www.github.com/Studio3T/robomongo/issues\""
    "PROJECT_VERSION=\"${PROJECT_VERSION}\"" "PROJECT_VERSION_SHORT=\"2.0\""
    "BUILD_NUMBER=\"modern\"" "PROJECT_NAME_LOWERCASE=\"robo3t\""
    "PROJECT_QT_VERSION=\"${Qt6_VERSION}\"" "MongoDB_VERSION=\"mongosh 2.11.1\""
    "MONGOSH_VERSION=\"2.11.1\"" "ROBO_MONGOC_VERSION=\"2.5.3\"" "NODE_VERSION=\"26.8.2\""
    "OPENSSL_VERSION=\"4.0.2\"" LIBSSH2_VERSION="1.11.1" "QSCINTILLA_VERSION=\"2.14.1\""
    "ROBOMONGO_NODE_EXECUTABLE=\"${ROBOMONGO_NODE_EXECUTABLE}\""
    "ROBOMONGO_RUNTIME_DIR=\"${ROBOMONGO_RUNTIME_DIR}\"")
add_subdirectory(src/robomongo/ssh)
add_subdirectory(src/robomongo)
include(CTest)
if(BUILD_TESTING)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${ROBOMONGO_DEPS_ROOT}/modern/googletest-1.18.0" "${CMAKE_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)
    add_subdirectory(src/robomongo-unit-tests)
endif()
