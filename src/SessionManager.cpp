#include "../include/SessionManager.h"
#include "common.h"
#include "util/mylog.h"
#include <algorithm>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ai_chat_sdk {
SessionManager::SessionManager(const std::string& dbName) : _dataManager(dbName) {

    auto sessions = _dataManager.getAllSessions();
    for (auto session : sessions) {
        _sessions[session->_sessionId] = session;
    }
}

std::string SessionManager::generateSessionId() {
    _sessionCounter.fetch_add(1);
    std::time_t time = std::time(nullptr);

    std::ostringstream os;
    os << "session_" << time << std::setw(8) << std::setfill('0') << _sessionCounter;
    return os.str();
}
std::string SessionManager::generateMessageId(size_t messageCounter) {
    messageCounter++;
    std::time_t time = std::time(nullptr);
    std::ostringstream os;
    os << "msg_" << time << "_" << std::setw(8) << std::setfill('0') << messageCounter;
    return os.str();
}

std::string SessionManager::createSession(const std::string& modelName) {
    // std::lock_guard<std::mutex> lock(_mutex);
    _mutex.lock();

    std::string sessionId = generateSessionId();
    auto session = std::make_shared<Session>(modelName);
    session->_sessionId = sessionId;
    session->_modelName = modelName;
    session->_createdAt = std::time(nullptr);
    session->_updatedAt = session->_createdAt;
    _sessions[sessionId] = session;
    _mutex.unlock();

    _dataManager.insertSession(*session);
    return sessionId;
}

std::shared_ptr<Session> SessionManager::getSession(const std::string& sessionId) {
    // std::lock_guard<std::mutex> lock(_mutex);

    // 先在内存中查找
    _mutex.lock();
    auto it = _sessions.find(sessionId);
    if (it != _sessions.end()) {
        _mutex.unlock();
        // 获取当前会话的历史消息
        it->second->_messages = _dataManager.getSessionMessages(sessionId);
        return it->second;
    }
    _mutex.unlock();

    // 内存中没有找到，从数据库中查找
    auto session = _dataManager.getSession(sessionId);
    if (session) {
        _mutex.lock();
        auto it = _sessions.find(sessionId);
        if (it == _sessions.end()) {
            // 内存中没有找到，将会话添加到会话列表
            _sessions[sessionId] = session;
        }
        _mutex.unlock();

        // 获取当前会话的历史消息
        session->_messages = _dataManager.getSessionMessages(sessionId);
        return session;
    }

    WRN("sessionId = {} not found", sessionId);
    return nullptr;
}

bool SessionManager::addMessage(const std::string& sessionId, const Message& message) {
    // std::lock_guard<std::mutex> lock(_mutex);

    _mutex.lock();
    auto it = _sessions.find(sessionId);
    if (it == _sessions.end()) {
        _mutex.unlock();
        return false;
    }

    Message msg(message._role, message._content);
    msg._messageId = generateMessageId(it->second->_messages.size());
    msg._timestamp = std::time(nullptr);
    INFO("message Info: content {}  timestamap {}", msg._content, msg._timestamp);

    it->second->_messages.push_back(msg);
    it->second->_updatedAt = std::time(nullptr);
    INFO("add message success, sessionId = {}, message.content = {}", sessionId, msg._content);
    _mutex.unlock();

    _dataManager.insertMessage(sessionId, msg);

    return true;
}

std::vector<Message> SessionManager::getHistoryMessages(const std::string& sessionId) const {
    // 先从内存中获取 内存中获取不到再从数据库中获取
    //  std::lock_guard<std::mutex> lock(_mutex);

    _mutex.lock();
    auto it = _sessions.find(sessionId);
    if (it != _sessions.end()) {
        _mutex.unlock();
        return it->second->_messages;
    }
    _mutex.unlock();

    return _dataManager.getSessionMessages(sessionId);
}

void SessionManager::updateSessionTimestamp(const std::string& sessionId) {
    // std::lock_guard<std::mutex> lock(_mutex);
    _mutex.lock();
    auto it = _sessions.find(sessionId);
    if (it != _sessions.end()) {
        it->second->_updatedAt = std::time(nullptr);
    }
    _mutex.unlock();

    _dataManager.updateSessionTimestamp(sessionId, it->second->_updatedAt);
}

std::vector<std::string> SessionManager::getSessionLists() const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<std::pair<std::time_t, std::shared_ptr<Session>>> temp;
    temp.reserve(_sessions.size());
    for (const auto& pair : _sessions) {
        temp.emplace_back(pair.second->_updatedAt, pair.second);
    }
    std::sort(temp.begin(), temp.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<std::string> sessionIds;
    sessionIds.reserve(_sessions.size());
    for (const auto& pari : temp) {
        sessionIds.push_back(pari.second->_sessionId);
    }
    return sessionIds;
}

bool SessionManager::deleteSession(const std::string& sessionId) {
    // std::lock_guard<std::mutex> lock(_mutex);
    _mutex.lock();
    auto it = _sessions.find(sessionId);
    if (it == _sessions.end()) {
        _mutex.unlock();
        return false;
    }
    _sessions.erase(it);
    _mutex.unlock();

    _dataManager.deleteSession(sessionId);
    return true;
}

void SessionManager::clearAllSessions() {
    // std::lock_guard<std::mutex> lock(_mutex);
    _mutex.lock();
    _sessions.clear();
    _mutex.unlock();

    _dataManager.clearAllSessions();
}

size_t SessionManager::getSessionCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _sessions.size();
}

} // namespace ai_chat_sdk