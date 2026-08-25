#pragma once

#include "LLMProvider.h"
namespace ai_chat_sdk {

class ollamaLLMProvider : public LLMProvider {
public:
    virtual bool initModel(const std::map<std::string, std::string>& modelConfig) override;
    virtual std::string getModelName() const override;
    virtual std::string getModelDesc() const override;
    virtual bool isAvailable() const override;

    virtual std::string sendMessage(
        const std::vector<Message>& messages,
        const std::map<std::string, std::string>& requestParam) override;

    virtual std::string sendMessageStream(
        const std::vector<Message>& messages,
        const std::map<std::string, std::string>& requestParam,
        std::function<void(const std::string&, bool)> callback) override;

protected:
    std::string _modelName;
    std::string _modelDesc;
};

} // namespace ai_chat_sdk
