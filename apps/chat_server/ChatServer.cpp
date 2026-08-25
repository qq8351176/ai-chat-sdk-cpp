#include "ChatServer.h"
#include "common.h"
#include "util/mylog.h"
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <httplib.h>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string resolveWebRoot() {
    namespace fs = std::filesystem;

    std::vector<fs::path> candidates;
    std::error_code error;

    const auto currentDirectory = fs::current_path(error);
    if (!error) {
        candidates.emplace_back(currentDirectory / "www");
    }

#if defined(__linux__)
    error.clear();
    const auto executablePath = fs::read_symlink("/proc/self/exe", error);
    if (!error) {
        const auto executableDirectory = executablePath.parent_path();
        candidates.emplace_back(executableDirectory / "www");
        candidates.emplace_back(executableDirectory / ".." / "share" / "AIChatServer" / "www");
    }
#endif

#if defined(AI_CHAT_SERVER_INSTALL_WEB_DIR)
    candidates.emplace_back(AI_CHAT_SERVER_INSTALL_WEB_DIR);
#endif

    for (const auto& candidate : candidates) {
        error.clear();
        if (!fs::is_directory(candidate, error) || error) {
            continue;
        }

        error.clear();
        if (!fs::is_regular_file(candidate / "index.html", error) || error) {
            continue;
        }

        error.clear();
        const auto canonicalPath = fs::weakly_canonical(candidate, error);
        return error ? candidate.lexically_normal().string() : canonicalPath.string();
    }

    return {};
}

} // namespace

namespace ai_chat_server {

ChatServer::ChatServer(const ServerConfig& config) : _config(config) {
    _chatSDK = std::make_shared<ai_chat_sdk::ChatSDK>();

    auto deepseekConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    deepseekConfig->_modelName = "deepseek-chat";
    deepseekConfig->_apiKey = config.deepseekAPIKey;
    deepseekConfig->_temperature = config.temperature;
    deepseekConfig->_maxTokens = config.maxTokens;

    auto chatGPTConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    chatGPTConfig->_modelName = "chatgpt";
    chatGPTConfig->_apiKey = config.chatGPTAPIKey;
    chatGPTConfig->_temperature = config.temperature;
    chatGPTConfig->_maxTokens = config.maxTokens;

    auto geminiConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    geminiConfig->_modelName = "gemini";
    geminiConfig->_apiKey = config.geminiAPIKey;
    geminiConfig->_temperature = config.temperature;
    geminiConfig->_maxTokens = config.maxTokens;

    // Ollama本地接入deepseek-r1:1.5b
    auto ollamaConfig = std::make_shared<ai_chat_sdk::OllamaConfig>();
    ollamaConfig->_modelName = config.ollamaModelName;
    ollamaConfig->_modelDesc = config.ollamaModelDesc;
    ollamaConfig->_endpoint = config.ollamaEndpoint;
    ollamaConfig->_temperature = config.temperature;
    ollamaConfig->_maxTokens = config.maxTokens;

    std::vector<std::shared_ptr<ai_chat_sdk::Config>> modelConfigs = {
        deepseekConfig, chatGPTConfig, geminiConfig, ollamaConfig
    };

    INFO("start init ChatSDK models...");
    if (!_chatSDK->initModels(modelConfigs)) {
        ERR("ChatSDK init Failed!!!");
        return;
    }
    INFO("ChatSDK models init success!!!");

    // 创建http服务器
    _chatServer = std::make_unique<httplib::Server>();
    if (!_chatServer) {
        ERR("ChatServer init Failed!!!");
        return;
    }
}

bool ChatServer::start() {
    if (!_chatServer || _isRunning.exchange(true)) {
        return false;
    }

    // 设置路由规则
    setHttpRoutes();

    // 静态资源优先从运行目录或可执行文件旁加载，同时兼容 cmake --install。
    const auto webRoot = resolveWebRoot();
    if (webRoot.empty()) {
        WRN("ChatServer web root not found; API routes remain available");
    } else if (!_chatServer->set_mount_point("/", webRoot)) {
        WRN("ChatServer failed to mount web root: {}", webRoot);
    } else {
        INFO("ChatServer web root: {}", webRoot);
    }

    // 为了不阻塞主线程，服务器在单独的线程中运行
    _serverThread = std::thread([this]() {
        INFO("ChatServer start on {} :{}", _config.host, _config.port);
        if (!_chatServer->listen(_config.host, _config.port)) {
            ERR("ChatServer failed to listen on {} :{}", _config.host, _config.port);
        }
        _isRunning.store(false);
    });
    INFO("ChatServer start success!!!");
    return true;
}

void ChatServer::stop() {
    if (_chatServer && _isRunning.load()) {
        _chatServer->stop();
    }

    if (_serverThread.joinable()) {
        _serverThread.join();
    }

    _isRunning.store(false);
    INFO("ChatServer stop success!!!");
}

bool ChatServer::isRunning() const {
    return _isRunning.load();
}

ChatServer::~ChatServer() {
    if (_chatServer && _isRunning.load()) {
        _chatServer->stop();
    }
    if (_serverThread.joinable()) {
        _serverThread.join();
    }
}

std::string ChatServer::buildResponse(const std::string& message, bool success) {
    Json::Value responseJson;
    responseJson["success"] = success;
    responseJson["message"] = message;

    // 序列化
    Json::StreamWriterBuilder writerBuilder;
    return Json::writeString(writerBuilder, responseJson);
}

void ChatServer::handleCreateSessionRequest(const httplib::Request& request, httplib::Response& response) {
    // 处理创建会话请求
    Json::Value requestJson;
    Json::Reader reader;
    if (!reader.parse(request.body, requestJson)) {
        std::string errJson = buildResponse("parse request body failed, json format error");

        Json::StreamWriterBuilder writerBuilder;
        std::string errJsonStr = Json::writeString(writerBuilder, errJson);

        response.status = 400;
        response.set_content(errJsonStr, "application/json");

        ERR("Failed to parse request body as JSON!!!");
        return;
    }

    std::string modelName = requestJson.get("model", "deepseek-chat").asString();
    std::string sessionID = _chatSDK->createSession(modelName);
    if (sessionID.empty()) {
        std::string errJson = buildResponse("Failed to create session!!!", false);

        Json::StreamWriterBuilder writerBuilder;
        std::string errJsonStr = Json::writeString(writerBuilder, errJson);

        response.status = 500;
        response.set_content(errJsonStr, "application/json");

        ERR("Failed to create session!!!");
        return;
    }
    // 构建响应体
    Json::Value dataJson;
    dataJson["sessionID"] = sessionID;
    dataJson["model"] = modelName;

    Json::Value responseJson;
    responseJson["success"] = true;
    responseJson["sessionID"] = sessionID;
    responseJson["data"] = dataJson;

    Json::StreamWriterBuilder writerBuilder;
    std::string responseJsonStr = Json::writeString(writerBuilder, responseJson);
    response.status = 200;
    response.set_content(responseJsonStr, "application/json");
}

void ChatServer::handleGetSessionListsRequest(const httplib::Request&, httplib::Response& response) {
    std::vector<std::string> sessionIDs = _chatSDK->getSessionLists();

    Json::Value dataArray(Json::arrayValue);
    for (const auto& sessionID : sessionIDs) {
        auto session = _chatSDK->getSession(sessionID);
        if (session) {
            Json::Value sessionJson;
            sessionJson["id"] = session->_sessionId;
            sessionJson["model"] = session->_modelName;
            sessionJson["created_at"] = static_cast<Json::Int64>(session->_createdAt);
            sessionJson["updated_at"] = static_cast<Json::Int64>(session->_updatedAt);
            sessionJson["message_count"] = static_cast<Json::UInt64>(session->_messages.size());
            if (!session->_messages.empty()) {
                sessionJson["first_user_message"] = session->_messages.front()._content;
            }
            dataArray.append(sessionJson);
        }
    }

    Json::Value responseJson;
    responseJson["success"] = true;
    responseJson["message"] = "get session  list success";
    responseJson["data"] = dataArray;

    Json::StreamWriterBuilder writerBuilder;
    std::string responseJsonStr = Json::writeString(writerBuilder, responseJson);

    response.status = 200;
    response.set_content(responseJsonStr, "application/json");
}

void ChatServer::handleGetModelListsRequest(const httplib::Request&, httplib::Response& response) {
    const auto modelLists = _chatSDK->getAvailableModels();

    Json::Value dataArray(Json::arrayValue);
    for (const auto& modelInfo : modelLists) {
        Json::Value modelJson;
        modelJson["name"] = modelInfo._modelName;
        modelJson["desc"] = modelInfo._modelDesc;
        dataArray.append(modelJson);
    }

    Json::Value responseJson;
    responseJson["success"] = true;
    responseJson["message"] = "get model lists success";
    responseJson["data"] = dataArray;

    Json::StreamWriterBuilder writerBuilder;
    response.status = 200;
    response.set_content(Json::writeString(writerBuilder, responseJson), "application/json");
}

void ChatServer::handleDeleteSessionRequest(const httplib::Request& request, httplib::Response& response) {
    std::string sessionID = request.matches[1];

    bool ret = _chatSDK->deleteSession(sessionID);
    if (ret) {
        std::string errJson = buildResponse("delete session success", true);
        response.status = 200;
        response.set_content(errJson, "application/json");
    } else {
        std::string errJson = buildResponse("delte session failed,session not found");
        response.status = 404;
        response.set_content(errJson, "application/json");
    }
}

void ChatServer::handleGetHistoryMessagesRequest(const httplib::Request& request, httplib::Response& response) {
    std::string sessionID = request.matches[1];

    auto session = _chatSDK->getSession(sessionID);
    if (!session) {
        auto errJson = buildResponse("session not found");
        response.status = 404;
        response.set_content(errJson, "application/json");
        return;
    }

    Json::Value dataArray(Json::arrayValue);
    for (const auto& message : session->_messages) {
        Json::Value messageJson;
        messageJson["id"] = message._messageId;
        messageJson["content"] = message._content;
        messageJson["role"] = message._role;
        messageJson["timestamp"] = static_cast<Json::Int64>(message._timestamp);
        dataArray.append(messageJson);
    }

    Json::Value responseJson;
    responseJson["success"] = true;
    responseJson["message"] = "get history messages success";
    responseJson["data"] = dataArray;

    Json::StreamWriterBuilder writerBuilder;
    std::string responseJsonStr = Json::writeString(writerBuilder, responseJson);

    response.status = 200;
    response.set_content(responseJsonStr, "application/json");
}

void ChatServer::handleSendMessageRequest(const httplib::Request& request, httplib::Response& response) {
    // 获取请求参数
    Json::Value requestJson;
    Json::Reader reader;
    if (!reader.parse(request.body, requestJson)) {
        std::string errorJsonStr = buildResponse("parse request body failed, json format error");
        response.status = 400; // 解析请求参数失败
        response.set_content(errorJsonStr, "application/json");
        return;
    }

    // 解析请求参数
    std::string sessionId = requestJson["session_id"].asString();
    std::string message = requestJson["message"].asString();
    if (sessionId.empty() || message.empty()) {
        std::string errorJsonStr = buildResponse("session_id or message is empty");
        response.status = 400; // 解析请求参数失败
        response.set_content(errorJsonStr, "application/json");
        return;
    }

    // 发送消息
    std::string assistantMessage = _chatSDK->sendMessage(sessionId, message);
    if (assistantMessage.empty()) {
        std::string errorJsonStr = buildResponse("Failed to send AI response message");
        response.status = 500; // 发送消息失败
        response.set_content(errorJsonStr, "application/json");
        return;
    }

    // 构造响应参数
    Json::Value dataJson;
    dataJson["session_id"] = sessionId;
    dataJson["response"] = assistantMessage;
    dataJson["data"]["assistant_message"] = assistantMessage;

    // 构建响应体
    Json::Value responseJson;
    responseJson["success"] = true;
    responseJson["message"] = "send message success";
    responseJson["data"] = dataJson;

    // 序列化
    Json::StreamWriterBuilder writerBuilder;
    std::string responseJsonStr = Json::writeString(writerBuilder, responseJson);

    response.status = 200; // 成功
    response.set_content(responseJsonStr, "application/json");
}

void ChatServer::handleSendMessageStreamRequest(const httplib::Request& request, httplib::Response& response) {
    Json::Value requestJson;
    Json::Reader reader;
    if (!reader.parse(request.body, requestJson)) {
        std::string errJsonStr = buildResponse("parse request body failed,json format erroe");
        response.status = 400;
        response.set_content(errJsonStr, "application/json");
        return;
    }

    std::string sessionId = requestJson["session_id"].asString();
    std::string message = requestJson["message"].asString();
    if (sessionId.empty() || message.empty()) {
        std::string errorJsonStr = buildResponse("session_id or message is empty");
        response.status = 400; // 解析请求参数失败
        response.set_content(errorJsonStr, "application/json");
        return;
    }

    response.status = 200;                                    // 成功
    response.set_header("Cache-Control", "no-cache");         // 不使用缓存，服务器立即将数据发送到网络
    response.set_header("Connection", "keep-alive");          // 保持连接，服务器不会关闭连接
    response.set_header("Access-Control-Allow-Origin", "*");  // 允许跨域请求
    response.set_header("Access-Control-Allow-Headers", "*"); // 允许所有请求头

    response.set_chunked_content_provider(
        "text/event-stream", [this, sessionId, message](size_t offset, httplib::DataSink& DataSink) -> bool {
            auto writeChunk = [&](const std::string& chunk, bool last) {
                if (last) {
                    std::string doneData = "data: [DONE]\n\n";
                    DataSink.write(doneData.c_str(), doneData.size());
                    DataSink.done();
                    return false;
                }

                if (chunk.empty()) {
                    return true;
                }

                std::string ssedata = "data: " + Json::valueToQuotedString(chunk.c_str()) + "\n\n";
                DataSink.write(ssedata.c_str(), ssedata.size());
                return true;
            };

            _chatSDK->sendMessageStream(sessionId, message, writeChunk);

            return false;
        }
    );
}
// 设置HTTP路由规则
void ChatServer::setHttpRoutes() {
    // 处理创建会话请求
    _chatServer->Post("/api/session", [this](const httplib::Request& request, httplib::Response& response) {
        handleCreateSessionRequest(request, response);
    });

    // 处理获取会话列表请求
    _chatServer->Get("/api/sessions", [this](const httplib::Request& request, httplib::Response& response) {
        handleGetSessionListsRequest(request, response);
    });

    // 处理获取模型列表请求
    _chatServer->Get("/api/models", [this](const httplib::Request& request, httplib::Response& response) {
        handleGetModelListsRequest(request, response);
    });

    // 处理删除会话请求
    _chatServer->Delete("/api/session/(.*)", [this](const httplib::Request& request, httplib::Response& response) {
        handleDeleteSessionRequest(request, response);
    });

    // 处理获取历史消息请求
    _chatServer->Get("/api/session/(.*)/history", [this](const httplib::Request& request, httplib::Response& response) {
        handleGetHistoryMessagesRequest(request, response);
    });

    // 处理发送消息请求-全量返回
    _chatServer->Post("/api/message", [this](const httplib::Request& request, httplib::Response& response) {
        handleSendMessageRequest(request, response);
    });

    // 处理发送消息请求-增量返回
    _chatServer->Post("/api/message/async", [this](const httplib::Request& request, httplib::Response& response) {
        handleSendMessageStreamRequest(request, response);
    });
}

} // namespace ai_chat_server
