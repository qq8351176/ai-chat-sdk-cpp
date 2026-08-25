#pragma once

#include "common.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ai_chat_sdk {

class LLMProvider {
public:
    virtual ~LLMProvider() = default;

    virtual bool initModel(const std::map<std::string, std::string>& modelConfig) = 0;
    virtual std::string getModelName() const = 0;
    virtual std::string getModelDesc() const = 0;
    virtual bool isAvailable() const = 0;

    virtual std::string sendMessage(
        const std::vector<Message>& messages,
        const std::map<std::string, std::string>& requestParam) = 0;

    virtual std::string sendMessageStream(
        const std::vector<Message>& messages,
        const std::map<std::string, std::string>& requestParam,
        std::function<void(const std::string&, bool)> callback) = 0;

protected:
    bool _isAvailable = false; // 标记模型是否有效
    std::string _apiKey;       // API密钥
    std::string _endpoint;     // 模型API endpoint  base url
};

} // namespace ai_chat_sdk
