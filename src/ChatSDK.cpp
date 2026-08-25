#include "ChatSDK.h"
#include "ChatGPTProvider.h"
#include "DeepSeekProvider.h"
#include "GeminiProvider.h"
#include "common.h"
#include "ollamaLLMProvider.h"
#include "util/mylog.h"
#include <algorithm>
#include <memory>

namespace ai_chat_sdk {

bool ChatSDK::initModels(const std::vector<std::shared_ptr<Config>>& configs) {

    // 注册所有模型
    registerAllProvider(configs);
    // 初始化模型提供者
    initProviders(configs);
    _initialized = true;
    return true;
}

// 注册所有模型
void ChatSDK::registerAllProvider(const std::vector<std::shared_ptr<Config>>& configs) {
    if (!_llmManager.isModelAvailable("deepseek-chat")) {
        auto deepseekProvider = std::make_unique<DeepSeekProvider>();
        _llmManager.registerProvider("deepseek-chat", std::move(deepseekProvider));
        INFO("deepseek-provider registed successed");
    }

    if (!_llmManager.isModelAvailable("chatgpt")) {
        auto chatgptProvider = std::make_unique<ChatGPTProvider>();
        _llmManager.registerProvider("chatgpt", std::move(chatgptProvider));
        INFO("chatgpt-provider registed successed");
    }

    if (!_llmManager.isModelAvailable("gemini")) {
        auto geminiProvider = std::make_unique<GeminiProvider>();
        _llmManager.registerProvider("gemini", std::move(geminiProvider));
        INFO("gemini-provider registed successed");
    }

    // Ollama 模型名来自配置，不能写死；否则换一个本地模型就会注册名与初始化名不一致，
    // 导致该模型静默地不可用。
    for (const auto& config : configs) {
        auto ollamaConfig = std::dynamic_pointer_cast<OllamaConfig>(config);
        if (!ollamaConfig || ollamaConfig->_modelName.empty()) {
            continue;
        }
        if (_llmManager.isModelAvailable(ollamaConfig->_modelName)) {
            continue;
        }
        auto ollamaProvider = std::make_unique<ollamaLLMProvider>();
        _llmManager.registerProvider(ollamaConfig->_modelName, std::move(ollamaProvider));
        INFO("ollama-provider registed successed, modelName = {}", ollamaConfig->_modelName);
    }
}

void ChatSDK::initProviders(const std::vector<std::shared_ptr<Config>>& configs) {
    for (const auto& config : configs) {
        if (auto apiConfig = std::dynamic_pointer_cast<APIConfig>(config)) {
            if (apiConfig->_modelName == "deepseek-chat" || apiConfig->_modelName == "chatgpt" ||
                apiConfig->_modelName == "gemini") {
                initAPIModelProviders(apiConfig->_modelName, apiConfig);

            } else {
                ERR("model {} is not support", apiConfig->_modelName);
            }
        } else if (auto ollamaConfig = std::dynamic_pointer_cast<OllamaConfig>(config)) {
            initOllamaModelProviders(ollamaConfig->_modelName, ollamaConfig);
        } else {
            ERR("Config {} is not supported", config->_modelName);
        }
    }
}

bool ChatSDK::initAPIModelProviders(const std::string& modelName, const std::shared_ptr<APIConfig>& apiConfig) {
    if (modelName.empty() || !apiConfig) {
        ERR("modelName or apiConfig is empty");
        return false;
    }
    if (_llmManager.isModelAvailable(modelName)) {
        INFO("model {} is already available", modelName);
        return true;
    }
    std::map<std::string, std::string> modelParam;
    modelParam["api_key"] = apiConfig->_apiKey;
    if (!_llmManager.initModel(modelName, modelParam)) {
        ERR("init model {} failed", modelName);
        return false;
    }
    // 模型配置
    _modelConfigs[modelName] = apiConfig;
    INFO("ChatSDK::initAPIModelProviders: model {} init successed", modelName);
    return true;
}

bool ChatSDK::initOllamaModelProviders(
    const std::string& modelName, const std::shared_ptr<OllamaConfig>& ollamaConfig
) {
    if (modelName.empty() || !ollamaConfig) {
        ERR("modelName or ollamaConfig is empty");
        return false;
    }
    if (_llmManager.isModelAvailable(modelName)) {
        INFO("model {} is already available", modelName);
        return true;
    }

    std::map<std::string, std::string> modelParams;
    modelParams["model_name"] = modelName;
    modelParams["model_desc"] = ollamaConfig->_modelDesc;
    modelParams["endpoint"] = ollamaConfig->_endpoint;
    if (!_llmManager.initModel(modelName, modelParams)) {
        ERR("init model {} failed", modelName);
        return false;
    }
    // 模型配置
    _modelConfigs[modelName] = ollamaConfig;
    INFO("ChatSDK::initOllamaModelProviders: model {} init successed", modelName);
    return true;
}

std::string ChatSDK::createSession(const std::string& modelName) {
    if (!_initialized) {
        ERR("ChatSDK::createSession: SDK is not initialized");
        return "";
    }
    auto sessionId = _sessionManager.createSession(modelName);
    if (sessionId.empty()) {
        ERR("create session {} failed", modelName);
        return "";
    }

    INFO("ChatSDK::createSession: create session {} successed", sessionId);

    return sessionId;
}

std::shared_ptr<Session> ChatSDK::getSession(const std::string& sessionId) {
    if (!_initialized) {
        ERR("ChatSDK::getSession: SDK is not initialized");
        return nullptr;
    }

    auto session = _sessionManager.getSession(sessionId);
    if (!session) {
        ERR("get session {} failed", sessionId);
        return nullptr;
    }
    INFO("ChatSDK::getSession: get session {} successed", sessionId);
    return session;
}

std::vector<std::string> ChatSDK::getSessionLists() const {
    if (!_initialized) {
        ERR("ChatSDK::getSessionLists: SDK is not initialized");
        return {};
    }
    INFO("ChatSDK::getSessionLists: get session lists successed");
    return _sessionManager.getSessionLists();
}

bool ChatSDK::deleteSession(const std::string& sessionId) {
    if (!_initialized) {
        ERR("ChatSDK::deleteSession: SDK is not initialized");
        return false;
    }
    if (!_sessionManager.deleteSession(sessionId)) {
        ERR("delete session {} failed", sessionId);
        return false;
    }
    INFO("ChatSDK::deleteSession: delete session {} successed", sessionId);
    return true;
}

std::vector<ModelInfo> ChatSDK::getAvailableModels() const {
    if (!_initialized) {
        ERR("ChatSDK::getAvailableModels: SDK is not initialized");
        return {};
    }
    INFO("ChatSDK::getAvailableModels: get available models successed");
    return _llmManager.getAvailableModels();
}

std::string ChatSDK::sendMessage(const std::string& sessionId, const std::string& message) {
    if (!_initialized) {
        ERR("ChatSDK::sendMessage: SDK is not initialized");
        return "";
    }
    auto session = _sessionManager.getSession(sessionId);
    if (!session) {
        ERR("get session {} failed", sessionId);
        return "";
    }

    Message userMessage("user", message);
    _sessionManager.addMessage(sessionId, userMessage);
    auto historyMessages = _sessionManager.getHistoryMessages(sessionId);

    auto it = _modelConfigs.find(session->_modelName);
    if (it == _modelConfigs.end()) {
        ERR("model {} not found", session->_modelName);
        return "";
    }
    std::map<std::string, std::string> requestParam;
    requestParam["temperature"] = std::to_string(it->second->_temperature);
    requestParam["max_tokens"] = std::to_string(it->second->_maxTokens);

    auto response = _llmManager.sendMessage(session->_modelName, historyMessages, requestParam);
    if (response.empty()) {
        ERR("send message {} failed", sessionId);
        return "";
    }

    // 添加助手消息并更新会话时间
    Message assistantMessage("assistant", response);
    _sessionManager.addMessage(sessionId, assistantMessage);
    _sessionManager.updateSessionTimestamp(sessionId);
    INFO("ChatSDK::sendMessage: send message to model {} successed", session->_modelName);
    return response;
}

// 给模型发送消息 - 增量返回
std::string ChatSDK::sendMessageStream(
    const std::string& sessionId, const std::string& message, std::function<void(const std::string&, bool)> callback
) {
    // 检测SDK是否初始化成功
    if (!_initialized) {
        ERR("ChatSDK::sendMessageStream: SDK is not initialized");
        return "";
    }

    // 获取sessionId对应的session对象
    auto session = _sessionManager.getSession(sessionId);
    if (!session) {
        ERR("ChatSDK::sendMessageStream: session {} not found", sessionId);
        return "";
    }

    // 构造历史消息
    Message userMessage("user", message);
    _sessionManager.addMessage(sessionId, userMessage);
    auto historyMessages = _sessionManager.getHistoryMessages(sessionId);

    // 构建请求参数
    auto it = _modelConfigs.find(session->_modelName);
    if (it == _modelConfigs.end()) {
        ERR("ChatSDK::sendMessageStream: model {} not found", session->_modelName);
        return "";
    }
    std::map<std::string, std::string> requestParam;
    requestParam["temperature"] = std::to_string(it->second->_temperature);
    requestParam["max_tokens"] = std::to_string(it->second->_maxTokens);

    // 调用LLMManager发送消息
    auto response = _llmManager.sendMessageStream(session->_modelName, historyMessages, requestParam, callback);
    if (response.empty()) {
        ERR("ChatSDK::sendMessageStream: send message to model {} failed", session->_modelName);
        return "";
    }

    // 添加助手消息并更新会话时间
    Message assistantMessage("assistant", response);
    _sessionManager.addMessage(sessionId, assistantMessage);
    _sessionManager.updateSessionTimestamp(sessionId);
    INFO("ChatSDK::sendMessageStream: send message to model {} successed", session->_modelName);
    return response;
}

} // namespace ai_chat_sdk
