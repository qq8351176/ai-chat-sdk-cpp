#设置cmake最小版本
cmake_minimum_required(VERSION 3.10)

#设置项目名称
project(testLLM)

#设置c++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS_REQUIRED ON)

#设置构建类型的debug
set(CMAKE_BUILD_TYPE Debug)

#添加测试可执行文件
add_executable(testLLM
    testLLM.cpp
    ../src/ChatSDK.cpp
    ../src/DataManager.cpp
    ../src/DeepSeekProvider.cpp
    ../src/ChatGPTProvider.cpp
    ../src/GeminiProvider.cpp
    ../src/LLMManager.cpp
    ../src/ollamaLLMProvider.cpp
    ../src/SessionManager.cpp
    ../src/version.cpp
    ../src/util/mylog.cpp
)

#设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

#添加头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR} ${SQLite3_INCLUDE_DIRS})

#define CPPHTTPLIB_OPENSSL_SUPPORT
target_compile_definitions(testLLM PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

#链接库
target_link_libraries(testLLM PRIVATE
    jsoncpp
    fmt
    spdlog
    gtest
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    ${SQLite3_LIBRARIES}
)
