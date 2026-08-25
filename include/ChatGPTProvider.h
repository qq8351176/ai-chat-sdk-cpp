#pragma once

#include "LLMProvider.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ai_chat_sdk {

class ChatGPTProvider final : public LLMProvider {
public:
    bool initModel(const std::map<std::string, std::string>& modelConfig) override;

    std::string getModelName() const override;
    std::string getModelDesc() const override;
    bool isAvailable() const override;

    std::string sendMessage(
        const std::vector<Message>& messages,
        const std::map<std::string, std::string>& requestParam) override;

    std::string sendMessageStream(
        const std::vector<Message>& messages,
        const std::map<std::string, std::string>& requestParam,
        std::function<void(const std::string&, bool)> callback) override;

private:
    bool _isAvailable = false;
    std::string _apiKey;
    std::string _endpoint;
};

} // namespace ai_chat_sdk
