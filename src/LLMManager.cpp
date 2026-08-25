#include "../include/LLMManager.h"
#include "common.h"
#include "util/mylog.h"
#include <utility>
#include <vector>

namespace ai_chat_sdk {

// 注册LLM提供者
bool LLMManager::registerProvider(const std::string& modelName, std::unique_ptr<LLMProvider> provider) {
    // 参数检测
    if (!provider) {
        ERR("cannot register nullptr provider, modelName = {}", modelName);
        return false;
    }

    // 注意：unique_ptr是防拷贝的，此处只能通过move的方式将资源转移给当前对象
    _providers[modelName] = std::move(provider);

    // 添加模型信息
    _modelInfos[modelName] = ModelInfo(modelName);

    // 模型初始化成功
    INFO("register provider success, modelName = {}", modelName);
    return true;
}

// 初始化指定模型
bool LLMManager::initModel(const std::string& modelName, const std::map<std::string, std::string>& modelParam) {
    auto it = _providers.find(modelName);
    if (it == _providers.end()) {
        ERR("LLMManager::initModel: model not registered, modelName = {}", modelName);
        return false;
    }

    bool isInit = it->second->initModel(modelParam);
    if (!isInit) {
        ERR("LLMManager::initModel: failed to init model, modelName = {}", modelName);
        return false;
    } else {
        INFO("init model success, modelName = {}", modelName);
        _modelInfos[modelName]._isAvailable = true;
        _modelInfos[modelName]._modelDesc = it->second->getModelDesc();
        return true;
    }
    return isInit;
}

// 获取可用模型
std::vector<ModelInfo> LLMManager::getAvailableModels() const {
    std::vector<ModelInfo> models;
    for (const auto& it : _modelInfos) {
        if (it.second._isAvailable)
            models.push_back(it.second);
    }
    return models;
}

bool LLMManager::isModelAvailable(const std::string& modelName) const {
    auto it = _modelInfos.find(modelName);
    if (it == _modelInfos.end()) {
        return false; // 模型不存在,视为不可用
    }
    return it->second._isAvailable;
}

// 发送消息给指定模型
std::string LLMManager::sendMessage(
    const std::string& modelName,
    const std::vector<Message>& messages,
    const std::map<std::string, std::string>& requestParam
) {
    auto it = _providers.find(modelName);
    if (it == _providers.end()) {
        ERR("model provider not found, modelName = {}", modelName);
        return "";
    }
    if (!it->second->isAvailable()) {
        ERR("model not available, modelName = {}", modelName);
        return "";
    }

    return it->second->sendMessage(messages, requestParam);
}

// 发送消息流给指定模型
std::string LLMManager::sendMessageStream(
    const std::string& modelName,
    const std::vector<Message>& messages,
    const std::map<std::string, std::string>& requestParam,
    std::function<void(const std::string&, bool)>& callback
) {
    // 检测模型是否注册
    auto it = _providers.find(modelName);
    if (it == _providers.end()) {
        ERR("model provider not found, modelName = {}", modelName);
        return "";
    }

    // 检测模型是否可用
    if (!it->second->isAvailable()) {
        ERR("model not available, modelName = {}", modelName);
        return "";
    }

    // 模型已经注册，并且已经初始化成功
    return it->second->sendMessageStream(messages, requestParam, callback);
}

} // namespace ai_chat_sdk
