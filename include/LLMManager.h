#pragma once

#include "LLMProvider.h"
#include "common.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ai_chat_sdk {
class LLMManager {
public:
    // 注册LLM提供者
    bool registerProvider(const std::string& modelName, std::unique_ptr<LLMProvider> provider);
    // 初始化指定模型
    bool initModel(const std::string& modelName, const std::map<std::string, std::string>& modelParam);
    // 获取可用模型
    std::vector<ModelInfo> getAvailableModels() const;
    // 检查模型是否可用
    bool isModelAvailable(const std::string& modelName) const;
    // 发送消息给指定模型
    std::string sendMessage(
        const std::string& modelName,
        const std::vector<Message>& messages,
        const std::map<std::string, std::string>& requestParam
    );
    // 发送消息流给指定模型
    std::string sendMessageStream(
        const std::string& modelName,
        const std::vector<Message>& messages,
        const std::map<std::string, std::string>& requestParam,
        std::function<void(const std::string&, bool)>& callback
    );

private:
    // key 模型名称 value 模型提供者
    std::map<std::string, std::unique_ptr<LLMProvider>> _providers;
    std::map<std::string, ModelInfo> _modelInfos;
};

} // namespace ai_chat_sdk