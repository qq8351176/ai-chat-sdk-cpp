#include "../include/util/mylog.h"
#include "ChatGPTProvider.h"
#include "ChatSDK.h"
#include "DeepSeekProvider.h"
#include "GeminiProvider.h"
#include "SessionManager.h"
#include "common.h"
#include "ollamaLLMProvider.h"
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#if 0 
TEST(DeepseekProviderTest, sendMessage) {
    auto provider = std::make_shared<ai_chat_sdk::DeepSeekProvider>();

    std::map<std::string, std::string> modelParam;
    modelParam["api_key"] = std::getenv("deepseek_api_key");
    modelParam["endpoint"] = "https://api.deepseek.com";

    provider->initModel(modelParam);

    ASSERT_TRUE(provider != nullptr);

    std::map<std::string, std::string> requestParam = {
        {"temperature", "0.7"},
        {"max_tokens", "2048"},
    };

    std::vector<ai_chat_sdk::Message> messages;
    messages.push_back(ai_chat_sdk::Message("user", "你好"));

    auto writerChunk = [&](const std::string& chunk, bool streamFinish) {
        INFO("DeepSeekProvider sendMessageStream, chunk: {}", chunk);
        if (streamFinish) {
            INFO("[DONE]");
        }
    };
    // std::string response = provider->sendMessage(messages, requestParam);
    std::string response = provider->sendMessageStream(messages, requestParam, writerChunk);
    ASSERT_FALSE(response.empty());
    INFO("response: {}", response);
}

TEST(ChatGPTProvider, sendMessage) {
    auto provider = std::make_shared<ai_chat_sdk::ChatGPTProvider>();

    std::map<std::string, std::string> modelParam;
    modelParam["api_key"] = std::getenv("deepseek_api_key");
    modelParam["endpoint"] = "https://api.deepseek.com";
    // 使用deepseek代替openaiapi进行response测试
    provider->initModel(modelParam);

    ASSERT_TRUE(provider != nullptr);

    std::map<std::string, std::string> requestParam = {
        // {"temperature", "0.7"},
        {"max_output_tokens", "2048"},
    };

    std::vector<ai_chat_sdk::Message> messages;
    messages.push_back(ai_chat_sdk::Message("user", "你好"));

    auto writerChunk = [&](const std::string& chunk, bool streamFinish) {
        INFO("ChatGPTProvider sendMessageStream, chunk: {}", chunk);
        if (streamFinish) {
            INFO("[DONE]");
        }
    };
    // std::string response = provider->sendMessage(messages, requestParam);
    std::string response = provider->sendMessageStream(messages, requestParam, writerChunk);
    ASSERT_FALSE(response.empty());
    INFO("response: {}", response);
}


TEST(GeminiProviderTest, sendMessageStream) {
    const char* apiKey = std::getenv("gemini_api_key");
    ASSERT_TRUE(apiKey != nullptr && apiKey[0] != '\0')
        << "请先执行: export gemini_api_key='你的 Gemini API Key'";

    auto provider = std::make_shared<ai_chat_sdk::GeminiProvider>();
    ASSERT_TRUE(provider != nullptr);

    std::map<std::string, std::string> modelParam = {
        {"api_key", apiKey},
        {"endpoint", "https://generativelanguage.googleapis.com"},
    };

    ASSERT_TRUE(provider->initModel(modelParam));
    ASSERT_TRUE(provider->isAvailable());

    std::map<std::string, std::string> requestParam = {
        {"temperature", "0.7"},
        {"max_tokens", "2048"},
    };

    std::vector<ai_chat_sdk::Message> messages;
    messages.emplace_back("user", "你好");

    auto writerChunk = [&](const std::string& chunk, bool streamFinish) {
        INFO("GeminiProvider sendMessageStream, chunk: {}", chunk);
        if (streamFinish) {
            INFO("[DONE]");
        }
    };

    // std::string response = provider->sendMessageStream(messages, requestParam, writerChunk);
    std::string response = provider->sendMessage(messages, requestParam);
    ASSERT_FALSE(response.empty());
    INFO("response: {}", response);
}

TEST(ollamaLLMProvider, sendMessage) {
    auto provider = std::make_shared<ai_chat_sdk::ollamaLLMProvider>();
    ASSERT_TRUE(provider != nullptr);

    std::map<std::string, std::string> modelParam;
    modelParam["model_name"] = "deepseek-r1:1.5b";
    modelParam["model_desc"] =
        "本地部署deepseek-r1:1.5b模型，采用专家混合架构，专注于深度理解与推理";
    modelParam["endpoint"] = "http://localhost:11434";

    provider->initModel(modelParam);
    ASSERT_TRUE(provider->isAvailable());

    std::map<std::string, std::string> requestParam = {
        {"temperature", "0.7"}, {"max_tokens", "2048"}};
    std::vector<ai_chat_sdk::Message> messages;
    messages.push_back({"user", "你是谁？"});

    // 实例化DeepSeekProvider的对象
    // 调用sendMessage方法
    // std::string fullData = provider->sendMessage(messages, requestParam);
    // ASSERT_FALSE(fullData.empty());

    auto writeChunk = [&](const std::string& chunk, bool last) {
        INFO("chunk : {}", chunk);
        if (last) {
            INFO("[DONE]");
        }
    };
    std::string fullData = provider->sendMessageStream(messages, requestParam, writeChunk);
    ASSERT_FALSE(fullData.empty());
    INFO("response : {}", fullData);
}
#endif

// 测试ChatSDK
TEST(ChatSDKTest, sendMessage) {
    auto sdk = std::make_shared<ai_chat_sdk::ChatSDK>();
    ASSERT_TRUE(sdk != nullptr);

    // 配置支持的模型参数：云模型-deepseek-chat gpt-4o-mini gemini-2.0-flash   Ollama本地接入deepseek-r1:1.5b
    // deepseek-chat
    auto deepseekConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    ASSERT_TRUE(deepseekConfig != nullptr);
    deepseekConfig->_modelName = "deepseek-chat";
    const char* deepseekKey = std::getenv("deepseek_api_key");
    ASSERT_NE(deepseekKey, nullptr) << "请先执行: export deepseek_apikey='你的 key'";
    deepseekConfig->_apiKey = deepseekKey;
    deepseekConfig->_temperature = 0.7;
    deepseekConfig->_maxTokens = 2048;

    // gpt-4o-mini
    auto chatGPTConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    ASSERT_TRUE(chatGPTConfig != nullptr);
    chatGPTConfig->_modelName = "chatgpt";
    const char* chatgptKey = std::getenv("deepseek_api_key");
    ASSERT_NE(chatgptKey, nullptr) << "请先执行: export deepseek_api_key='你的 key'";
    chatGPTConfig->_apiKey = chatgptKey;
    chatGPTConfig->_temperature = 0.7;
    chatGPTConfig->_maxTokens = 2048;

    // gemini-2.0-flash
    auto geminiConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    ASSERT_TRUE(geminiConfig != nullptr);
    geminiConfig->_modelName = "gemini";
    const char* geminiKey = std::getenv("gemini_api_key");
    ASSERT_NE(geminiKey, nullptr) << "请先执行: export gemini_api_key='你的 key'";
    geminiConfig->_apiKey = geminiKey;
    geminiConfig->_temperature = 0.7;
    geminiConfig->_maxTokens = 2048;

    // Ollama本地接入deepseek-r1:1.5b
    auto ollamaConfig = std::make_shared<ai_chat_sdk::OllamaConfig>();
    ASSERT_TRUE(ollamaConfig != nullptr);
    ollamaConfig->_modelName = "deepseek-r1:1.5b";
    ollamaConfig->_modelDesc = "本地部署deepseek-r1:1.5b模型，采用专家混合架构，专注于深度理解与推理";
    ollamaConfig->_endpoint = "http://localhost:11434";
    ollamaConfig->_temperature = 0.7;
    ollamaConfig->_maxTokens = 2048;

    std::vector<std::shared_ptr<ai_chat_sdk::Config>> modelConfigs = {
        deepseekConfig, chatGPTConfig, geminiConfig, ollamaConfig
    };

    sdk->initModels(modelConfigs);

    // 创建会话
    auto sessionId = sdk->createSession(geminiConfig->_modelName);
    ASSERT_FALSE(sessionId.empty());

    std::string message;
    std::cout << ">>> ";
    std::getline(std::cin, message);
    auto response = sdk->sendMessage(sessionId, message);
    ASSERT_FALSE(response.empty());

    std::cout << ">>> ";
    std::getline(std::cin, message);
    sdk->sendMessage(sessionId, message);
    ASSERT_FALSE(response.empty());

    // 获取会话历史消息
    auto messages = sdk->_sessionManager.getHistoryMessages(sessionId);
    for (const auto& msg : messages) {
        std::cout << msg._role << ": " << msg._content << std::endl;
    }
    ASSERT_FALSE(messages.empty());
}

int main() {
    util::Logger::initLogger("spdlogtest", "stdout", spdlog::level::debug);
    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}
