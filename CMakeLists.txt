cmake_minimum_required(VERSION 3.16)

project(
    ai_chat_sdk
    VERSION 0.1.0
    DESCRIPTION "C++ SDK for accessing multiple large language models"
    LANGUAGES CXX
)

include(GNUInstallDirs)

option(AI_CHAT_SDK_BUILD_SERVER "Build the AIChatServer HTTP server" ON)
option(AI_CHAT_SDK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(spdlog CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

if(TARGET JsonCpp::JsonCpp)
    set(AI_CHAT_SDK_JSONCPP_TARGET JsonCpp::JsonCpp)
elseif(TARGET jsoncpp_lib)
    set(AI_CHAT_SDK_JSONCPP_TARGET jsoncpp_lib)
else()
    message(FATAL_ERROR "The jsoncpp package did not provide a supported CMake target")
endif()

find_path(
    AI_CHAT_SDK_HTTPLIB_INCLUDE_DIR
    NAMES httplib.h
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
)
if(NOT AI_CHAT_SDK_HTTPLIB_INCLUDE_DIR)
    message(FATAL_ERROR "httplib.h was not found; install cpp-httplib or place it in third_party/httplib")
endif()

set(AI_CHAT_SDK_PUBLIC_HEADERS
    include/ChatGPTProvider.h
    include/ChatSDK.h
    include/common.h
    include/DataManager.h
    include/DeepSeekProvider.h
    include/GeminiProvider.h
    include/LLMManager.h
    include/LLMProvider.h
    include/ollamaLLMProvider.h
    include/SessionManager.h
)

add_library(ai_chat_sdk STATIC
    src/ChatGPTProvider.cpp
    src/ChatSDK.cpp
    src/DataManager.cpp
    src/DeepSeekProvider.cpp
    src/GeminiProvider.cpp
    src/LLMManager.cpp
    src/ollamaLLMProvider.cpp
    src/SessionManager.cpp
    src/version.cpp
    src/util/mylog.cpp
    ${AI_CHAT_SDK_PUBLIC_HEADERS}
)
add_library(ai_chat_sdk::ai_chat_sdk ALIAS ai_chat_sdk)

target_compile_features(ai_chat_sdk PUBLIC cxx_std_17)
target_include_directories(ai_chat_sdk
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
    ${AI_CHAT_SDK_HTTPLIB_INCLUDE_DIR}
)

target_link_libraries(
    ai_chat_sdk
    PUBLIC
    spdlog::spdlog
    ${AI_CHAT_SDK_JSONCPP_TARGET}
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
)
# httplib 需要 OpenSSL 支持宏（https 请求）
target_compile_definitions(ai_chat_sdk PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

if(MSVC)
    target_compile_options(ai_chat_sdk PRIVATE /W4)
    if(AI_CHAT_SDK_WARNINGS_AS_ERRORS)
        target_compile_options(ai_chat_sdk PRIVATE /WX)
    endif()
else()
    target_compile_options(ai_chat_sdk PRIVATE -Wall -Wextra -Wpedantic)
    if(AI_CHAT_SDK_WARNINGS_AS_ERRORS)
        target_compile_options(ai_chat_sdk PRIVATE -Werror)
    endif()
endif()

if(AI_CHAT_SDK_BUILD_SERVER)
    add_subdirectory(apps/chat_server)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ai_chat_sdk_smoke_test tests/smoke_test.cpp)
    target_link_libraries(ai_chat_sdk_smoke_test PRIVATE ai_chat_sdk::ai_chat_sdk)
    add_test(NAME ai_chat_sdk.smoke COMMAND ai_chat_sdk_smoke_test)
    add_executable(
        ai_chat_sdk_logger_smoke_test
        tests/logger_smoke_test.cpp
    )

    target_link_libraries(
        ai_chat_sdk_logger_smoke_test
        PRIVATE ai_chat_sdk::ai_chat_sdk
    )

    add_test(
        NAME ai_chat_sdk.logger_smoke
        COMMAND ai_chat_sdk_logger_smoke_test
    )
endif()

include(CMakePackageConfigHelpers)

set(AI_CHAT_SDK_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/ai_chat_sdk)

install(
    TARGETS ai_chat_sdk
    EXPORT ai_chat_sdk_targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(
    FILES ${AI_CHAT_SDK_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ai_chat_sdk
)
install(
    FILES include/util/mylog.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ai_chat_sdk/util
)
install(
    FILES include/ai_chat_sdk/version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ai_chat_sdk
)
install(
    EXPORT ai_chat_sdk_targets
    FILE ai_chat_sdk_targets.cmake
    NAMESPACE ai_chat_sdk::
    DESTINATION ${AI_CHAT_SDK_INSTALL_CMAKEDIR}
)

configure_package_config_file(
    cmake/ai_chat_sdkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ai_chat_sdkConfig.cmake
    INSTALL_DESTINATION ${AI_CHAT_SDK_INSTALL_CMAKEDIR}
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ai_chat_sdkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ai_chat_sdkConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ai_chat_sdkConfigVersion.cmake
    DESTINATION ${AI_CHAT_SDK_INSTALL_CMAKEDIR}
)
