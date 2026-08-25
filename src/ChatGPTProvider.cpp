#include "ChatGPTProvider.h"
#include "util/mylog.h"
#include <httplib.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/writer.h>
#include <sstream>
#include <string>

namespace ai_chat_sdk {

bool ChatGPTProvider::initModel(const std::map<std::string, std::string>& modelConfig) {
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
    INFO("ChatGPTProvider initModel success, endpoint: {}", _endpoint);
    return true;
}
std::string ChatGPTProvider::getModelName() const {
    return "deepseek-v4-flash";
} // 使用deepseek代替openai api 进行response测试

std::string ChatGPTProvider::getModelDesc() const {
    return "ChatGPT is a large language model that can generate human-like text.";
}
bool ChatGPTProvider::isAvailable() const {
    return _isAvailable;
}
std::string ChatGPTProvider::sendMessage(
    const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam
) {
    // 检测模型是否可用
    if (!_isAvailable) {
        ERR("ChatGPTProvider is not available");
        return "";
    }
    // double temperature = 0.7;
    int maxOutTokens = 2048;

    // if (requestParam.find("temperature") != requestParam.end()) {
    //     temperature = std::stod(requestParam.at("temperature"));
    // }
    if (requestParam.find("max_output_tokens") != requestParam.end()) {
        maxOutTokens = std::stoi(requestParam.at("max_output_tokens"));
    }

    Json::Value messageArray(Json::arrayValue);
    for (const auto& message : messages) {
        Json::Value messageObject;
        messageObject["role"] = message._role;
        messageObject["content"] = message._content;
        messageArray.append(messageObject);
    }

    Json::Value requestBody;
    requestBody["model"] = getModelName();
    requestBody["input"] = messageArray;
    // requestBody["temperature"] = temperature;
    requestBody["max_output_tokens"] = maxOutTokens;

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

    httplib::Client client(_endpoint.c_str());
    client.set_connection_timeout(30);
    client.set_read_timeout(60);
    client.set_proxy("127.0.0.1", 10808);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + _apiKey},
        // {"Content-Type", "application/json"},
    };

    auto response = client.Post("/responses", headers, requestBodyStr, "application/json");
    // auto response = client.Post("/v1/responses", headers, requestBodyStr, "application/json");

    if (!response) {
        ERR("ChatGPTProvider sendMessage error, response is null");
        return "";
    }

    if (response->status != 200) {
        ERR("ChatGPTProvider sendMessage error, status: {}, body: {}", response->status, response->body);
        return "";
    }

    INFO("ChatGPTProvider sendMessage, response body: {}", response->body);

    Json::CharReaderBuilder readerBuilder;
    Json::Value responseJson;
    std::istringstream responseStream(response->body);
    std::string parseError;
    if (!Json::parseFromStream(readerBuilder, responseStream, &responseJson, &parseError)) {
        ERR("ChatGPTProvider sendMessage error, parse response body failed: {}", parseError);
        return "";
    }

    if (responseJson.isMember("output") && responseJson["output"].isArray() && !responseJson["output"].empty()) {
        auto output = responseJson["output"][0];
        if (output.isMember("content") && output["content"].isArray() && !output["content"].empty() &&
            output["content"][0].isMember("text")) {
            std::string replytString = output["content"][0]["text"].asString();
            INFO("ChatGPTProvider sendMessage, replytString: {}", replytString);
            return replytString;
        }
    }

    ERR("ChatGPTProvider sendMessage parse response body failed, errorJson: {}", parseError);

    return "";
}

std::string ChatGPTProvider::sendMessageStream(
    const std::vector<Message>& messages,
    const std::map<std::string, std::string>& requestParam,
    std::function<void(const std::string&, bool)> callback
) {
    // 检查模型是否可用
    if (!_isAvailable) {
        ERR("ChatGPTProvider is not available");
        return "";
    }

    // double temperature = 0.7;
    int maxOutTokens = 2048;

    // if (requestParam.find("temperature") != requestParam.end()) {
    //     temperature = std::stod(requestParam.at("temperature"));
    // }
    if (requestParam.find("max_output_tokens") != requestParam.end()) {
        maxOutTokens = std::stoi(requestParam.at("max_output_tokens"));
    }

    Json::Value messageArray(Json::arrayValue);
    for (const auto& message : messages) {
        Json::Value messageObject;
        messageObject["role"] = message._role;
        messageObject["content"] = message._content;
        messageArray.append(messageObject);
    }

    Json::Value requestBody;
    requestBody["model"] = getModelName();
    requestBody["input"] = messageArray;
    // requestBody["temperature"] = temperature;
    requestBody["max_output_tokens"] = maxOutTokens;
    requestBody["stream"] = true;

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

    httplib::Client client(_endpoint.c_str());
    client.set_connection_timeout(30);
    client.set_read_timeout(60);
    client.set_proxy("127.0.0.1", 10808);

    httplib::Headers headers = {
        {"Content-Type", "application/json"}, {"Authorization", "Bearer " + _apiKey}, {"Accept", "text/event-stream"}
    };

    std::string buffer;
    bool gotError = false;
    std::string errorMsg;
    int statuscode = 0;
    bool streamFinish = false;
    std::string fullResponse;

    httplib::Request req;
    req.method = "POST";
    req.path = "/responses";
    // req.path = "/v1/responses";
    req.headers = headers;
    req.body = requestBodyStr;

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
        INFO("chatgpt provider sendmessagestream.buffer: {}", buffer);

        size_t pos = 0;
        while ((pos = buffer.find("\n\n")) != std::string::npos) {
            // 截取当前找到的数据块
            std::string event = buffer.substr(0, pos);
            buffer.erase(0, pos + 2);

            std::istringstream eventStream(event);
            std::string eventType;
            std::string eventData;
            std::string line;
            while (std::getline(eventStream, line)) {
                if (line.empty() || line[0] == ':') {
                    continue;
                }
                if (line.compare(0, 6, "event:") == 0) {
                    eventType = line.substr(7);
                } else if ((line.compare(0, 5, "data:")) == 0) {
                    eventData = line.substr(6);
                }
            }

            Json::Value chunk;
            Json::CharReaderBuilder readerBuilder;
            std::string error;
            std::istringstream eventDataStream(eventData);
            if (!(Json::parseFromStream(readerBuilder, eventDataStream, &chunk, &error))) {
                WRN("ChatGPTProvider sendMessageStream error, parseError: {}", error);
                continue;
            }

            if (eventType == "response.output_text.delta") {
                if (chunk.isMember("delta") && chunk["delta"].isString()) {
                    std::string delta = chunk["delta"].asString();
                    callback(delta, false);
                }
            } else if (eventType == "response.output_item.done") {
                if (chunk.isMember("item") && chunk["item"].isObject()) {
                    Json::Value item = chunk["item"];
                    if (item.isMember("content") && item["content"].isArray() && !item["content"].empty() &&
                        item["content"][0].isMember("text") && item["content"][0]["text"].isString()) {
                        fullResponse += item["content"][0]["text"].asString();
                    }
                }

            } else if (eventType == "response.completed") {
                streamFinish = true;
                callback("", true);
                return true;
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
        WRN("ChatGPTProvider sendMessageStream error, stream not finish");
        callback("", true);
    }

    return fullResponse;
}
} // namespace ai_chat_sdk
