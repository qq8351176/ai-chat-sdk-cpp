#include "ChatServer.h"
#include "util/mylog.h"

#include <gflags/gflags.h>
#include <spdlog/common.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <thread>

DEFINE_string(host, "0.0.0.0", "服务器绑定地址");
DEFINE_int32(port, 8080, "服务器监听端口");
DEFINE_string(log_level, "INFO", "日志级别: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL");
DEFINE_double(temperature, 0.7, "模型温度值，范围为 0.0~2.0");
DEFINE_int32(max_tokens, 2048, "模型生成的最大 token 数，必须为正数");
DEFINE_string(config_file, "./ChatServer.conf", "gflags 配置文件路径");
DEFINE_string(ollama_model_name, "", "Ollama 模型名称");
DEFINE_string(ollama_model_desc, "", "Ollama 模型描述");
DEFINE_string(ollama_endpoint, "", "Ollama API 地址");

namespace {

constexpr const char* kVersion = "1.0.0";

bool hasArgument(int argc, char** argv, const std::string& shortName, const std::string& longName) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == shortName || argument == longName) {
            return true;
        }
    }
    return false;
}

bool findConfigFileArgument(
    int argc,
    char** argv,
    std::string& configFile,
    bool& explicitlySet,
    std::string& error
) {
    const std::string longPrefix = "--config_file=";
    const std::string shortPrefix = "-config_file=";

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument.rfind(longPrefix, 0) == 0 || argument.rfind(shortPrefix, 0) == 0) {
            const auto separator = argument.find('=');
            configFile = argument.substr(separator + 1);
            explicitlySet = true;
        } else if (argument == "--config_file" || argument == "-config_file") {
            if (i + 1 >= argc) {
                error = "--config_file 后缺少配置文件路径";
                return false;
            }
            configFile = argv[++i];
            explicitlySet = true;
        }
    }

    if (explicitlySet && configFile.empty()) {
        error = "配置文件路径不能为空";
        return false;
    }
    return true;
}

std::string getFirstEnvironmentValue(std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const char* value = std::getenv(name);
        if (value != nullptr && value[0] != '\0') {
            return value;
        }
    }
    return {};
}

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

bool parseLogLevel(const std::string& value, spdlog::level::level_enum& level) {
    const std::string normalized = toUpper(value);
    if (normalized == "TRACE") {
        level = spdlog::level::trace;
    } else if (normalized == "DEBUG") {
        level = spdlog::level::debug;
    } else if (normalized == "INFO") {
        level = spdlog::level::info;
    } else if (normalized == "WARN" || normalized == "WARNING") {
        level = spdlog::level::warn;
    } else if (normalized == "ERROR") {
        level = spdlog::level::err;
    } else if (normalized == "CRITICAL") {
        level = spdlog::level::critical;
    } else {
        return false;
    }
    return true;
}

bool validateConfig(const ai_chat_server::ServerConfig& config) {
    if (config.host.empty()) {
        std::cerr << "配置错误: host 不能为空。\n";
        return false;
    }
    if (config.port < 1 || config.port > 65535) {
        std::cerr << "配置错误: port 必须在 1~65535 之间，当前值为 " << config.port << "。\n";
        return false;
    }
    if (config.temperature < 0.0 || config.temperature > 2.0) {
        std::cerr << "配置错误: temperature 必须在 0.0~2.0 之间，当前值为 " << config.temperature << "。\n";
        return false;
    }
    if (config.maxTokens <= 0) {
        std::cerr << "配置错误: max_tokens 必须为正数，当前值为 " << config.maxTokens << "。\n";
        return false;
    }

    const bool hasCloudAPIKey = !config.deepseekAPIKey.empty() || !config.chatGPTAPIKey.empty()
        || !config.geminiAPIKey.empty();
    if (!hasCloudAPIKey) {
        std::cerr << "配置错误: DEEPSEEK_API_KEY、CHATGPT_API_KEY/OPENAI_API_KEY、"
                     "GEMINI_API_KEY 至少设置一个。\n";
        return false;
    }

    const bool hasAnyOllamaValue = !config.ollamaModelName.empty() || !config.ollamaModelDesc.empty()
        || !config.ollamaEndpoint.empty();
    const bool hasCompleteOllamaConfig = !config.ollamaModelName.empty() && !config.ollamaModelDesc.empty()
        && !config.ollamaEndpoint.empty();
    if (hasAnyOllamaValue && !hasCompleteOllamaConfig) {
        std::cerr << "配置错误: 启用 Ollama 时，ollama_model_name、ollama_model_desc 和 "
                     "ollama_endpoint 必须全部填写。\n";
        return false;
    }
    return true;
}

void printHelp(const char* programName) {
    std::cout
        << "AIChatServer " << kVersion << " - AI 聊天 HTTP 服务器\n\n"
        << "用法:\n"
        << "  " << programName << " [选项]\n\n"
        << "参数选项:\n"
        << "  -h, --help                    显示本帮助说明\n"
        << "  -v, --version                 显示版本号\n"
        << "  --host=0.0.0.0                服务器绑定地址\n"
        << "  --port=8080                   服务器监听端口\n"
        << "  --log_level=INFO              日志级别\n"
        << "  --temperature=0.7             温度值，范围 0.0~2.0\n"
        << "  --max_tokens=2048             最大 token 数，必须为正数\n"
        << "  --config_file=FILE            配置文件路径\n"
        << "  --ollama_model_name=NAME      Ollama 模型名称\n"
        << "  --ollama_model_desc=TEXT      Ollama 模型描述\n"
        << "  --ollama_endpoint=URL         Ollama API 地址\n\n"
        << "API Key 环境变量（至少设置一个）:\n"
        << "  DEEPSEEK_API_KEY\n"
        << "  CHATGPT_API_KEY（也支持 OPENAI_API_KEY）\n"
        << "  GEMINI_API_KEY\n\n"
        << "使用示例:\n"
        << "  export DEEPSEEK_API_KEY=your_api_key\n"
        << "  " << programName << "\n"
        << "  " << programName << " --port=9000 --temperature=0.5\n"
        << "  " << programName << " --config_file=./ChatServer.conf\n\n"
        << "HTTP 接口:\n"
        << "  POST   /api/session                         创建会话\n"
        << "  GET    /api/sessions                        获取会话列表\n"
        << "  GET    /api/models                          获取可用模型列表\n"
        << "  DELETE /api/session/{session_id}            删除会话\n"
        << "  GET    /api/session/{session_id}/history    获取历史消息\n"
        << "  POST   /api/message                         发送消息（完整响应）\n"
        << "  POST   /api/message/async                   发送消息（SSE 流式响应）\n";
}

} // namespace

int main(int argc, char** argv) {
    gflags::SetUsageMessage("AIChatServer - AI 聊天 HTTP 服务器");
    gflags::SetVersionString(kVersion);

    if (hasArgument(argc, argv, "-h", "--help")) {
        printHelp(argv[0]);
        return 0;
    }
    if (hasArgument(argc, argv, "-v", "--version")) {
        std::cout << "AIChatServer " << kVersion << '\n';
        return 0;
    }

    std::string configFile = FLAGS_config_file;
    bool configFileExplicitlySet = false;
    std::string configArgumentError;
    if (!findConfigFileArgument(
            argc, argv, configFile, configFileExplicitlySet, configArgumentError)) {
        std::cerr << "参数错误: " << configArgumentError << '\n';
        return 1;
    }

    FLAGS_config_file = configFile;
    std::ifstream configInput(configFile);
    if (configInput.good()) {
        configInput.close();
        if (!gflags::ReadFromFlagsFile(configFile, argv[0], false)) {
            std::cerr << "配置错误: 无法解析配置文件 " << configFile << "。\n";
            return 1;
        }
    } else if (configFileExplicitlySet) {
        std::cerr << "配置错误: 找不到配置文件 " << configFile << "。\n";
        return 1;
    }

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    ai_chat_server::ServerConfig config;
    config.host = FLAGS_host;
    config.port = FLAGS_port;
    config.logLevel = toUpper(FLAGS_log_level);
    config.temperature = FLAGS_temperature;
    config.maxTokens = FLAGS_max_tokens;
    config.ollamaModelName = FLAGS_ollama_model_name;
    config.ollamaModelDesc = FLAGS_ollama_model_desc;
    config.ollamaEndpoint = FLAGS_ollama_endpoint;

    config.deepseekAPIKey = getFirstEnvironmentValue(
        {"DEEPSEEK_API_KEY", "deepseek_api_key", "deepseek_apikey"});
    config.chatGPTAPIKey = getFirstEnvironmentValue(
        {"CHATGPT_API_KEY", "OPENAI_API_KEY", "chatgpt_api_key", "chatgpt_apikey"});
    if (config.chatGPTAPIKey.empty()) {
        // ChatGPTProvider 当前通过 DeepSeek 兼容代理访问模型，未单独配置时复用 DeepSeek Key。
        config.chatGPTAPIKey = config.deepseekAPIKey;
    }
    config.geminiAPIKey = getFirstEnvironmentValue(
        {"GEMINI_API_KEY", "gemini_api_key", "gemini_apikey_new"});

    spdlog::level::level_enum logLevel = spdlog::level::info;
    if (!parseLogLevel(config.logLevel, logLevel)) {
        std::cerr << "配置错误: 不支持的 log_level: " << config.logLevel << "。\n";
        return 1;
    }
    if (!validateConfig(config)) {
        return 1;
    }

    try {
        util::Logger::initLogger("AIChatServer", "stdout", logLevel);

        INFO("AIChatServer version {}", kVersion);
        INFO("config file: {}", configFile);
        INFO("listen address: {}:{}", config.host, config.port);
        INFO("log level: {}, temperature: {}, max tokens: {}", config.logLevel, config.temperature, config.maxTokens);
        INFO(
            "API keys: DeepSeek={}, ChatGPT={}, Gemini={}",
            config.deepseekAPIKey.empty() ? "not set" : "set",
            config.chatGPTAPIKey.empty() ? "not set" : "set",
            config.geminiAPIKey.empty() ? "not set" : "set"
        );

        ai_chat_server::ChatServer server(config);
        if (!server.start()) {
            ERR("ChatServer start failed");
            return 1;
        }

        while (server.isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "AIChatServer 异常: " << exception.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "AIChatServer 发生未知异常。\n";
        return 1;
    }
}
