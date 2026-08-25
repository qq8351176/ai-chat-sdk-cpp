cmake_minimum_required(VERSION 3.16)

# This directory supports both of these build modes:
#   1. Built by the repository's top-level CMakeLists.txt with add_subdirectory().
#   2. Built independently against an installed ai_chat_sdk package.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(AIChatServer VERSION 1.0.0 LANGUAGES CXX)
    include(GNUInstallDirs)

    find_package(ai_chat_sdk CONFIG REQUIRED)

    # Installed SDK headers live under <prefix>/include/ai_chat_sdk, while the
    # in-tree target exposes the source include directory directly.
    find_path(
        AI_CHAT_SERVER_SDK_INCLUDE_DIR
        NAMES ChatSDK.h
        PATH_SUFFIXES ai_chat_sdk
        REQUIRED
    )
endif()

find_package(gflags CONFIG REQUIRED)
if(TARGET gflags::gflags)
    set(AI_CHAT_SERVER_GFLAGS_TARGET gflags::gflags)
elseif(TARGET gflags)
    set(AI_CHAT_SERVER_GFLAGS_TARGET gflags)
else()
    message(FATAL_ERROR "The gflags package did not provide a supported CMake target")
endif()

if(NOT AI_CHAT_SDK_HTTPLIB_INCLUDE_DIR)
    find_path(
        AI_CHAT_SDK_HTTPLIB_INCLUDE_DIR
        NAMES httplib.h
        HINTS ${CMAKE_CURRENT_LIST_DIR}/../../third_party/httplib
        REQUIRED
    )
endif()

add_executable(
    AIChatServer
    main.cpp
    ChatServer.cpp
    ChatServer.h
)

target_compile_features(AIChatServer PRIVATE cxx_std_17)
target_compile_definitions(
    AIChatServer
    PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
    AI_CHAT_SERVER_INSTALL_WEB_DIR="${CMAKE_INSTALL_FULL_DATADIR}/AIChatServer/www"
)
target_include_directories(
    AIChatServer
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${AI_CHAT_SDK_HTTPLIB_INCLUDE_DIR}
    ${AI_CHAT_SERVER_SDK_INCLUDE_DIR}
)
target_link_libraries(
    AIChatServer
    PRIVATE
    ai_chat_sdk::ai_chat_sdk
    ${AI_CHAT_SERVER_GFLAGS_TARGET}
)

# Keep the executable and its default configuration together in the build tree.
set_target_properties(
    AIChatServer
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
configure_file(
    ChatServer.conf
    ${PROJECT_BINARY_DIR}/ChatServer.conf
    COPYONLY
)
add_custom_target(
    AIChatServerWebAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/www"
            "$<TARGET_FILE_DIR:AIChatServer>/www"
    DEPENDS AIChatServer
    COMMENT "Copying AIChatServer web assets"
)

install(TARGETS AIChatServer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
    FILES ChatServer.conf
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/AIChatServer
)
install(
    DIRECTORY www/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/AIChatServer/www
)
