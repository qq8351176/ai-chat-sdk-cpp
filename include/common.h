#pragma once
#include <ctime>
#include <string>
#include <vector>

namespace ai_chat_sdk {

struct Message {
    std::string _messageId; // 消息ID
    std::string _role;      // 角色，如user、assistant等
    std::string _content;   // 消息内容
    std::time_t _timestamp; // 消息发送时间戳

    // 构造函数
    Message(const std::string& role = "", const std::string& content = "")
        : _role(role), _content(content), _timestamp(0) {}
};
// 配置结构体
struct Config {
    std::string _modelName; // 模型名称
    double _temperature = 0.7;
    int _maxTokens = 2048;

    virtual ~Config() = default;
};

// 通过API方式接入云端模型
struct APIConfig : public Config {
    std::string _apiKey; // API密钥
};

// 通过Ollama接入本地模型---不需要apikey
struct OllamaConfig : public Config {
    std::string _modelName; // 模型名称
    std::string _modelDesc; // 模型描述
    std::string _endpoint;  // 模型API endpoint  base url
};

// LLM信息
struct ModelInfo {
    std::string _modelName;
    std::string _modelDesc;
    std::string _Provider;
    std::string _endpoint;     // 模型端点
    bool _isAvailable = false; // 是否可用

    ModelInfo(
        const std::string& modelName = "",
        const std::string& modelDesc = "",
        const std::string& Provider = "",
        const std::string& endpoint = ""
    )
        : _modelName(modelName), _modelDesc(modelDesc), _Provider(Provider), _endpoint(endpoint) {};
};

struct Session {
    std::string _sessionId;
    std::string _modelName;
    std::vector<Message> _messages;
    std::time_t _createdAt;
    std::time_t _updatedAt;
    Session(const std::string& modelName = "") : _modelName(modelName) {};
};

} // namespace ai_chat_sdk