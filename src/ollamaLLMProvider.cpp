#include "ollamaLLMProvider.h"
#include "util/mylog.h"
#include <httplib.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/writer.h>
#include <sstream>
#include <string>
namespace ai_chat_sdk {
bool ollamaLLMProvider::initModel(const std::map<std::string, std::string>& modelConfig) {
    // 初始化模型名称
    if (modelConfig.find("model_name") == modelConfig.end()) {
        ERR("OllamaLLMProvider::initModel: model_name is not found in modelConfig");
        return false;
    }
    _modelName = modelConfig.at("model_name");

    // 初始化模型描述
    if (modelConfig.find("model_desc") == modelConfig.end()) {
        ERR("OllamaLLMProvider::initModel: model_desc is not found in modelConfig");
        return false;
    }
    _modelDesc = modelConfig.at("model_desc");

    // 初始化endpoint
    if (modelConfig.find("endpoint") == modelConfig.end()) {
        ERR("OllamaLLMProvider::initModel: endpoint is not found in modelConfig");
        return false;
    }
    _endpoint = modelConfig.at("endpoint");

    // 初始化模型是否有效
    _isAvailable = true;
    return true;
}

std::string ollamaLLMProvider::getModelName() const { return _modelName; }

std::string ollamaLLMProvider::getModelDesc() const { return _modelDesc; }

bool ollamaLLMProvider::isAvailable() const { return _isAvailable; }
std::string ollamaLLMProvider::sendMessage(
    const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam) {
    if (!isAvailable()) {
        ERR("OllamaLLMProvider::sendMessage: model is not available");
        return "";
    }

    int max_tokens = 2048;
    float temperature = 0.7;
    if (requestParam.find("max_tokens") != requestParam.end()) {
        max_tokens = std::stoi(requestParam.at("max_tokens"));
    }
    if (requestParam.find("temperature") != requestParam.end()) {
        temperature = std::stod(requestParam.at("temperature"));
    }

    Json::Value message_array(Json::arrayValue);
    for (const auto& message : messages) {
        Json::Value msg;
        msg["role"] = message._role;
        msg["content"] = message._content;
        message_array.append(msg);
    }

    Json::Value options;
    options["temperature"] = temperature;
    options["num_ctx"] = max_tokens;

    Json::Value requestBody;
    requestBody["model"] = getModelName();
    requestBody["messages"] = message_array;
    requestBody["stream"] = false;
    requestBody["options"] = options;

    Json::StreamWriterBuilder writerBuilder;
    std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

    httplib::Client client(_endpoint.c_str());
    client.set_connection_timeout(30, 0);
    client.set_read_timeout(60, 0);
    httplib::Headers header = {{"Content-Type", "application/json"}};
    auto response = client.Post("/api/chat", header, requestBodyStr, "application/json");
    if (!response) {
        ERR("OllamaLLMProvider::sendMessage: failed to send request, error: {}",
            to_string(response.error()));
        return "";
    }

    INFO("OllamaLLMProvider::sendMessage: response status: {}", response->status);
    INFO("OllamaLLMProvider::sendMessage: response body: {}", response->body);
    if (response->status != 200) {
        ERR("OllamaLLMProvider::sendMessage: failed to send request, status: {}", response->status);
        return "";
    }

    Json::Value responseBody;
    Json::CharReaderBuilder reader;
    std::istringstream responseStream(response->body);
    std::string errors;
    if (!Json::parseFromStream(reader, responseStream, &responseBody, &errors)) {
        ERR("OllamaLLMProvider::sendMessage: failed to parse response body, errors: {}", errors);
        return "";
    }

    std::string modelResponse = "";
    if (responseBody.isMember("message") && responseBody["message"].isObject() &&
        responseBody["message"].isMember("content")) {
        modelResponse = responseBody["message"]["content"].asString();
        INFO("OllamaLLMProvider::sendMessage: modelResponse: {}", modelResponse);
        return modelResponse;
    }

    // 处理其他情况
    ERR("OllamaLLMProvider::sendMessage: invalid response format");
    return "";
}

std::string ollamaLLMProvider::sendMessageStream(
    const std::vector<Message>& messages,
    const std::map<std::string, std::string>& requestParam,
    std::function<void(const std::string&, bool)> callback) {

    if (!isAvailable()) {
        ERR("OllamaLLMProvider::sendMessageStream: model is not available");
        return "";
    }

    float temperature = 0.7f;
    int maxTokens = 1024;
    if (requestParam.find("temperature") != requestParam.end()) {
        temperature = std::stof(requestParam.at("temperature"));
    }
    if (requestParam.find("max_tokens") != requestParam.end()) {
        maxTokens = std::stoi(requestParam.at("max_tokens"));
    }

    Json::Value messageArray(Json::arrayValue);
    for (const auto& message : messages) {
        Json::Value messageObject(Json::objectValue);
        messageObject["role"] = message._role;
        messageObject["content"] = message._content;
        messageArray.append(messageObject);
    }

    Json::Value requestBody;
    requestBody["model"] = getModelName();
    requestBody["messages"] = messageArray;
    requestBody["stream"] = true;
    Json::Value options;
    options["temperature"] = temperature;
    options["num_ctx"] = maxTokens;
    requestBody["options"] = options;

    Json::StreamWriterBuilder writerBuilder;
    std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

    httplib::Client client(_endpoint);
    client.set_read_timeout(60, 0);
    client.set_connection_timeout(30, 0);

    httplib::Headers headers = {{"Content-Type", "application/json"}};

    std::string buffer;
    bool gotError = false;
    std::string errorMsg;
    int statusCode = 0;
    bool streamFinish = false;
    std::string fullData;

    httplib::Request request;
    request.method = "POST";
    request.path = "/api/chat";
    request.headers = headers;
    request.body = requestBodyStr;
    request.response_handler = [&](const httplib::Response& res) {
        statusCode = res.status;
        if (statusCode != 200) {
            gotError = true;
            errorMsg = "OllamaLLMProvider::sendMessageStream: failed to send request, status: " +
                       std::to_string(statusCode);
            return false; // 终止请求
        }

        return true;
    };

    request.content_receiver = [&](const char* data,
                                   size_t data_length,
                                   uint64_t offset,
                                   uint64_t total_length) {
        if (gotError)
            return false;

        buffer.append(data, data_length);
        size_t pos = 0;
        while ((pos = buffer.find("\n")) != std::string::npos) {
            std::string chunk = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (chunk.empty())
                continue;

            Json::Value chunkJson;
            Json::CharReaderBuilder reader;
            std::string error;
            std::istringstream chunkStream(chunk);
            if (!Json::parseFromStream(reader, chunkStream, &chunkJson, &error)) {
                ERR("OllamaLLMProvider::sendMessageStream: failed to parse chunk json, errors: {}",
                    error);
                continue;
            }
            // 处理结束标记
            if (chunkJson.get("done", false).asBool()) {
                streamFinish = true;
                callback("", true);
                return true;
            }

            // 提取增量数据
            if (chunkJson.isMember("message") && chunkJson["message"].isMember("content")) {
                std::string delta = chunkJson["message"]["content"].asString();
                fullData += delta;
                callback(delta, false);
            }
        }
        return true;
    };
    // 给Ollama服务器发送请求
    auto response = client.send(request);
    if (!response) {
        ERR("OllamaLLMProvider::sendMessageStream: failed to send request, error: {}",
            to_string(response.error()));
        return "";
    }

    // 确保流式响应正常结束
    if (!streamFinish) {
        ERR("OllamaLLMProvider::sendMessageStream: stream not finish, fullData: {}", fullData);
        callback("", true);
    }

    return fullData;
}

} // namespace ai_chat_sdk