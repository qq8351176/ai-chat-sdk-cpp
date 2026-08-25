#include "DeepSeekProvider.h"
#include "util/mylog.h"
#include <httplib.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/writer.h>
#include <sstream>
#include <string>

namespace ai_chat_sdk {

bool DeepSeekProvider::initModel(const std::map<std::string, std::string>& modelConfig) {
    _isAvailable = false;

    auto it = modelConfig.find("api_key");
    if (it == modelConfig.end() || it->second.empty()) {
        ERR("api_key not found in model_config");
        return false;
    }
    _apiKey = it->second;

    // 初始化Base URL
    it = modelConfig.find("endpoint");
    if (it == modelConfig.end()) {
        _endpoint = "https://api.deepseek.com";
    } else {
        _endpoint = it->second;
    }

    _isAvailable = true;
    INFO("DeepSeekProvider initModel success, endpoint: {}", _endpoint);
    return true;
}

bool DeepSeekProvider::isAvailable() const {
    return _isAvailable;
}

std::string DeepSeekProvider::getModelName() const {
    return "deepseek-v4-flash";
}

std::string DeepSeekProvider::getModelDesc() const {
    return "deepseek model 国产大模型";
}

std::string DeepSeekProvider::sendMessage(
    const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam
) {

    if (!_isAvailable) {
        ERR("DeepSeekProvider isAvailable is false");
        return "";
    }

    double temperature = 0.7;
    int maxTokens = 2048;

    if (requestParam.find("temperature") != requestParam.end()) {
        temperature = std::stod(requestParam.at("temperature"));
    }
    if (requestParam.find("max_tokens") != requestParam.end()) {
        maxTokens = std::stoi(requestParam.at("max_tokens"));
    }

    Json::Value messageArray(Json::arrayValue);
    for (const auto& message : messages) {
        Json::Value messageObject;
        messageObject["role"] = message._role;
        messageObject["content"] = message._content;
        messageArray.append(messageObject);
    }

    // 构造请求体
    Json::Value requestBody;
    requestBody["model"] = getModelName();
    requestBody["messages"] = messageArray;
    requestBody["temperature"] = temperature;
    requestBody["max_tokens"] = maxTokens;

    // 序列化
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

    INFO("DeepSeekProvider sendMessage, requestBody: {}", requestBodyStr);

    // 使用cpp-httplib构造http客户端
    httplib::Client client(_endpoint.c_str());
    client.set_connection_timeout(30);
    client.set_read_timeout(30);

    // 设置请求头
    httplib::Headers headers = {{"Content-Type", "application/json"}, {"Authorization", "Bearer " + _apiKey}};

    // 发送POST请求
    auto response = client.Post("/v1/chat/completions", headers, requestBodyStr, "application/json");
    if (!response) {
        ERR("DeepSeekProvider sendMessage error, response is null");
        return "";
    }
    INFO("DeepSeekProvider sendMessage, response: {}", response->status);
    INFO("DeepSeekProvider sendMessage, response body: {}", response->body);

    if (response->status != 200) {
        return "";
    }

    // 解析
    Json::Value responseBody;
    Json::CharReaderBuilder readerBuilder;
    std::string parseError;
    std::istringstream responseStream(response->body);
    if (Json::parseFromStream(readerBuilder, responseStream, &responseBody, &parseError)) {
        if (responseBody.isMember("choices") && responseBody["choices"].isArray() &&
            !responseBody["choices"][0].empty()) {
            auto choices = responseBody["choices"][0];
            if (choices.isMember("message") && !choices["message"].empty()) {
                std::string replyContent = choices["message"]["content"].asString();
                INFO("DeepSeekProvider sendMessage, replyContent: {}", replyContent);
                return replyContent;
            }
        }
    }

    ERR("DeepSeekProvider sendMessage error, parseError: {}", parseError);

    return "deepseek response failed";
}

std::string DeepSeekProvider::sendMessageStream(
    const std::vector<Message>& messages,
    const std::map<std::string, std::string>& requestParam,
    std::function<void(const std::string&, bool)> callback
) {

    if (!_isAvailable) {
        ERR("DeepSeekProvider isAvailable is false");
        return "";
    }

    // 构造请求参数
    double temperature = 0.7;
    int maxTokens = 2048;

    if (requestParam.find("temperature") != requestParam.end()) {
        temperature = std::stod(requestParam.at("temperature"));
    }
    if (requestParam.find("max_tokens") != requestParam.end()) {
        maxTokens = std::stoi(requestParam.at("max_tokens"));
    }

    Json::Value messageArray(Json::arrayValue);
    for (const auto& message : messages) {
        Json::Value messageObject;
        messageObject["role"] = message._role;
        messageObject["content"] = message._content;
        messageArray.append(messageObject);
    }

    // 构造请求体
    Json::Value requestBody;
    requestBody["model"] = getModelName();
    requestBody["messages"] = messageArray;
    requestBody["temperature"] = temperature;
    requestBody["max_tokens"] = maxTokens;
    requestBody["stream"] = true;

    // 序列化
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

    INFO("DeepSeekProvider sendMessageStream, requestBody: {}", requestBodyStr);

    // 使用cpp-httplib 构造http客户端
    httplib::Client client(_endpoint.c_str());
    client.set_connection_timeout(30, 0);
    client.set_read_timeout(300, 0);

    // 设置请求头
    httplib::Headers headers = {
        {"Content-Type", "application/json"}, {"Authorization", "Bearer " + _apiKey}, {"Accept", "text/event-stream"}
    };

    std::string buffer;
    bool gotError = false;
    std::string errorMsg;
    int statuscode = 0;
    bool streamFinish = false;
    std::string fullResponse;

    // 创建请求对象
    httplib::Request req;
    req.method = "POST";
    req.path = "/v1/chat/completions";
    req.headers = headers;
    req.body = requestBodyStr;
    // 设置响应的处理器
    req.response_handler = [&](const httplib::Response& res) {
        statuscode = res.status;
        if (statuscode != 200) {
            gotError = true;
            errorMsg = "http status code error: " + std::to_string(statuscode);
            return false;
        }
        return true;
    };
    req.content_receiver = [&](const char* data, size_t len, size_t offset, size_t total) {
        if (gotError)
            return false;
        buffer.append(data, len);
        INFO("deepseek provider sendmessagestream.buffer: {}", buffer);

        // 处理所有的流式响应的数据块 数据块之间是\n\n分离的
        size_t pos = 0;
        while ((pos = buffer.find("\n\n")) != std::string::npos) {
            // 截取当前找到的数据块
            std::string chunk = buffer.substr(0, pos);
            buffer.erase(0, pos + 2);
            if (chunk.empty() || chunk[0] == ':') {
                continue;
            }

            if (chunk.compare(0, 6, "data: ") == 0) {
                std::string modelData = chunk.substr(6);
                if (modelData == "[DONE]") {
                    callback("", true);
                    streamFinish = true;
                    return true;
                }
                // 反序列化
                Json::Value modelDataJson;
                Json::CharReaderBuilder reader;
                std::string error;
                std::istringstream modelDataStream(modelData);

                if (Json::parseFromStream(reader, modelDataStream, &modelDataJson, &error)) {
                    if (modelDataJson.isMember("choices") && modelDataJson["choices"].isArray() &&
                        !modelDataJson["choices"].empty() && modelDataJson["choices"][0].isMember("delta")) {
                        const Json::Value& content = modelDataJson["choices"][0]["delta"]["content"];
                        if (content.isString() && !content.empty()) {
                            const std::string contentText = content.asString();
                            fullResponse += contentText;
                            callback(contentText, false);
                        }
                    }
                } else {
                    WRN("DeepSeekProvider sendMessageStream error, parseError: {}", error);
                }
            }
        }
        return true;
    };

    // 发送请求
    auto result = client.send(req);
    if (!result) {
        ERR("Network error: {}", httplib::to_string(result.error()));
        return "";
    }

    if (!streamFinish) {
        WRN("DeepSeekProvider sendMessageStream error, stream not finish");
        callback("", true);
    }

    return fullResponse;
}

} // namespace ai_chat_sdk
