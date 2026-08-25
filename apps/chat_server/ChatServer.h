#pragma once
#include "ChatSDK.h"
#include "httplib.h"
#include <atomic>
#include <memory>
#include <thread>

namespace ai_chat_server {
struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    std::string logLevel = "INFO";

    double temperature = 0.7;
    int maxTokens = 2048;

    std::string deepseekAPIKey; // deepseek API Key
    std::string geminiAPIKey;   // gemini API Key
    std::string chatGPTAPIKey;  // chatGPT API Key

    // Ollama
    std::string ollamaModelName; // Ollama模型名称
    std::string ollamaModelDesc; // Ollama模型描述
    std::string ollamaEndpoint;  // Ollama API 地址
};

class ChatServer {
public:
    ChatServer(const ServerConfig& config);
    bool start();
    void stop();
    bool isRunning() const;

    ~ChatServer();

private:
    // 构造响应
    std::string buildResponse(const std::string& message, bool success = false);
    // 处理创建会话请求
    void handleCreateSessionRequest(const httplib::Request& request, httplib::Response& response);
    // 处理获取会话列表请求
    void handleGetSessionListsRequest(const httplib::Request& request, httplib::Response& response);
    // 处理获取模型列表请求
    void handleGetModelListsRequest(const httplib::Request& request, httplib::Response& response);
    // 处理删除会话请求
    void handleDeleteSessionRequest(const httplib::Request& request, httplib::Response& response);
    // 处理获取历史消息请求
    void handleGetHistoryMessagesRequest(const httplib::Request& request, httplib::Response& response);
    // 处理发送消息请求-全量返回
    void handleSendMessageRequest(const httplib::Request& request, httplib::Response& response);
    // 处理发送消息请求-增量返回
    void handleSendMessageStreamRequest(const httplib::Request& request, httplib::Response& response);

    // 设置HTTP路由规则
    void setHttpRoutes();

private:
    ServerConfig _config;
    std::unique_ptr<httplib::Server> _chatServer = nullptr;
    std::shared_ptr<ai_chat_sdk::ChatSDK> _chatSDK = nullptr;
    std::atomic<bool> _isRunning = false;
    std::thread _serverThread;
};

} // namespace ai_chat_server
