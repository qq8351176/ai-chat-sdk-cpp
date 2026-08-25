"use strict";

const API = Object.freeze({
    sessions: "/api/sessions",
    models: "/api/models",
    createSession: "/api/session",
    streamMessage: "/api/message/async",
    sessionHistory: (sessionId) => `/api/session/${encodeURIComponent(sessionId)}/history`,
    deleteSession: (sessionId) => `/api/session/${encodeURIComponent(sessionId)}`,
});

const MAX_MESSAGE_LENGTH = 2000;

const state = {
    sessions: [],
    models: [],
    activeSessionId: null,
    activeModel: null,
    isSending: false,
    isCreatingSession: false,
    streamAbortController: null,
    historyRequestToken: 0,
    sessionSettlingUntil: 0,
};

const elements = {};

document.addEventListener("DOMContentLoaded", initialize);

function initialize() {
    Object.assign(elements, {
        sidebar: document.querySelector("#sidebar"),
        sidebarBackdrop: document.querySelector("#sidebar-backdrop"),
        mobileMenuButton: document.querySelector("#mobile-menu-button"),
        sessionList: document.querySelector("#session-list"),
        sessionCount: document.querySelector("#session-count"),
        serviceState: document.querySelector("#service-state"),
        serviceStateText: document.querySelector("#service-state-text"),
        welcomeView: document.querySelector("#welcome-view"),
        chatView: document.querySelector("#chat-view"),
        currentSessionTitle: document.querySelector("#current-session-title"),
        currentModelBadge: document.querySelector("#current-model-badge"),
        messagesViewport: document.querySelector("#messages-viewport"),
        messages: document.querySelector("#messages"),
        messageForm: document.querySelector("#message-form"),
        messageInput: document.querySelector("#message-input"),
        characterCount: document.querySelector("#character-count"),
        sendButton: document.querySelector("#send-button"),
        modelDialog: document.querySelector("#model-dialog"),
        modelForm: document.querySelector("#model-form"),
        modelGrid: document.querySelector("#model-grid"),
        confirmModelButton: document.querySelector("#confirm-model-button"),
        toastRegion: document.querySelector("#toast-region"),
        messageTemplate: document.querySelector("#message-template"),
    });

    bindEvents();
    updateComposerState();
    loadSessions();

    window.setInterval(() => {
        if (state.sessions.length > 0) {
            renderSessions();
        }
    }, 60_000);
}

function bindEvents() {
    document.querySelectorAll('[data-action="new-chat"]').forEach((button) => {
        button.addEventListener("click", openModelDialog);
    });

    document.querySelectorAll('[data-action="close-model-dialog"]').forEach((button) => {
        button.addEventListener("click", closeModelDialog);
    });

    elements.modelDialog.addEventListener("cancel", (event) => {
        event.preventDefault();
        closeModelDialog();
    });

    elements.modelDialog.addEventListener("click", (event) => {
        if (event.target === elements.modelDialog) {
            closeModelDialog();
        }
    });

    elements.modelForm.addEventListener("submit", createSelectedSession);
    elements.modelGrid.addEventListener("change", handleModelSelection);
    elements.sessionList.addEventListener("click", handleSessionListClick);
    elements.messageForm.addEventListener("submit", sendMessage);
    elements.messageInput.addEventListener("input", handleComposerInput);
    elements.messageInput.addEventListener("keydown", handleComposerKeydown);
    elements.messages.addEventListener("click", handleMessageAreaClick);

    elements.mobileMenuButton.addEventListener("click", () => {
        document.body.classList.add("sidebar-open");
    });
    elements.sidebarBackdrop.addEventListener("click", closeSidebar);
}

async function apiJson(url, options = {}) {
    const headers = new Headers(options.headers || {});
    if (options.body && !headers.has("Content-Type")) {
        headers.set("Content-Type", "application/json");
    }
    headers.set("Accept", "application/json");

    const response = await fetch(url, { ...options, headers });
    const raw = await response.text();
    let payload = null;

    if (raw) {
        try {
            payload = JSON.parse(raw);
            if (typeof payload === "string") {
                payload = JSON.parse(payload);
            }
        } catch {
            payload = null;
        }
    }

    if (!response.ok || payload?.success === false) {
        const message = payload?.message || `请求失败（HTTP ${response.status}）`;
        throw new Error(message);
    }

    return payload || {};
}

async function loadSessions({ silent = false } = {}) {
    if (!silent) {
        renderSessionLoading();
    }

    try {
        const response = await apiJson(API.sessions);
        state.sessions = Array.isArray(response.data)
            ? response.data.map(normalizeSession).sort(sortSessionsByUpdatedTime)
            : [];
        setServiceStatus("online", "本地服务已连接");
        renderSessions();
        syncActiveSessionFromList();
    } catch (error) {
        setServiceStatus("offline", "本地服务连接失败");
        if (!silent) {
            renderSessionError(error.message);
            showToast(`无法获取会话列表：${error.message}`, "error");
        }
    }
}

function normalizeSession(session) {
    return {
        id: String(session?.id || session?.session_id || session?.sessionID || ""),
        model: String(session?.model || "未知模型"),
        created_at: Number(session?.created_at || 0),
        updated_at: Number(session?.updated_at || session?.created_at || 0),
        message_count: Number(session?.message_count || 0),
        first_user_message: String(session?.first_user_message || ""),
    };
}

function sortSessionsByUpdatedTime(left, right) {
    return right.updated_at - left.updated_at;
}

function renderSessionLoading() {
    elements.sessionList.replaceChildren();
    for (let index = 0; index < 3; index += 1) {
        const skeleton = document.createElement("div");
        skeleton.className = "session-skeleton";
        skeleton.setAttribute("aria-hidden", "true");
        skeleton.innerHTML = "<span></span><span></span><span></span>";
        elements.sessionList.append(skeleton);
    }
}

function renderSessionError(message) {
    const empty = document.createElement("div");
    empty.className = "session-empty";
    empty.textContent = message || "暂时无法加载会话";
    elements.sessionList.replaceChildren(empty);
    elements.sessionCount.textContent = "0";
}

function renderSessions() {
    const fragment = document.createDocumentFragment();

    if (state.sessions.length === 0) {
        const empty = document.createElement("div");
        empty.className = "session-empty";
        empty.textContent = "还没有历史会话，创建一个开始探索吧。";
        fragment.append(empty);
    } else {
        state.sessions.forEach((session) => {
            const item = document.createElement("div");
            item.className = "session-item";
            item.dataset.sessionId = session.id;
            item.setAttribute("role", "listitem");
            if (session.id === state.activeSessionId) {
                item.classList.add("is-active");
            }

            const selectButton = document.createElement("button");
            selectButton.type = "button";
            selectButton.className = "session-select";
            selectButton.dataset.action = "select-session";
            selectButton.dataset.sessionId = session.id;
            selectButton.setAttribute("aria-label", `打开会话：${getSessionTitle(session)}`);

            const time = document.createElement("time");
            time.className = "session-time";
            time.dateTime = toIsoDate(session.updated_at);
            time.textContent = formatSessionTime(session.updated_at);

            const title = document.createElement("span");
            title.className = "session-title";
            title.textContent = getSessionTitle(session);

            const model = document.createElement("span");
            model.className = "session-model";
            model.textContent = session.model;

            selectButton.append(time, title, model);

            const deleteButton = document.createElement("button");
            deleteButton.type = "button";
            deleteButton.className = "session-delete";
            deleteButton.dataset.action = "delete-session";
            deleteButton.dataset.sessionId = session.id;
            deleteButton.setAttribute("aria-label", `删除会话：${getSessionTitle(session)}`);
            deleteButton.title = "删除会话";
            deleteButton.textContent = "...";

            item.append(selectButton, deleteButton);
            fragment.append(item);
        });
    }

    elements.sessionList.replaceChildren(fragment);
    elements.sessionCount.textContent = String(state.sessions.length);
}

function getSessionTitle(session) {
    return session.first_user_message.trim() || "尚未开始的对话";
}

function syncActiveSessionFromList() {
    if (!state.activeSessionId) {
        return;
    }
    const activeSession = state.sessions.find((session) => session.id === state.activeSessionId);
    if (!activeSession) {
        showWelcome();
        return;
    }
    state.activeModel = activeSession.model;
    updateWorkspaceHeading(activeSession);
}

async function handleSessionListClick(event) {
    const actionButton = event.target.closest("button[data-action]");
    if (!actionButton) {
        return;
    }

    const sessionId = actionButton.dataset.sessionId;
    if (!sessionId) {
        return;
    }

    if (actionButton.dataset.action === "select-session") {
        await selectSession(sessionId);
    } else if (actionButton.dataset.action === "delete-session") {
        await deleteSession(sessionId);
    }
}

async function selectSession(sessionId) {
    if (state.isSending) {
        showToast("请等待当前回复完成后再切换会话。", "error");
        return;
    }

    const session = state.sessions.find((item) => item.id === sessionId);
    if (!session) {
        showToast("该会话已不存在，请刷新后重试。", "error");
        return;
    }

    state.activeSessionId = session.id;
    state.activeModel = session.model;
    showChat();
    updateWorkspaceHeading(session);
    renderSessions();
    closeSidebar();
    await loadHistory(session.id);
}

async function loadHistory(sessionId) {
    const requestToken = ++state.historyRequestToken;
    renderHistoryLoading();

    try {
        const response = await apiJson(API.sessionHistory(sessionId));
        if (requestToken !== state.historyRequestToken || state.activeSessionId !== sessionId) {
            return;
        }
        const messages = Array.isArray(response.data) ? response.data : [];
        renderHistory(messages);
    } catch (error) {
        if (requestToken !== state.historyRequestToken) {
            return;
        }
        renderHistoryError(error.message);
        showToast(`无法获取历史消息：${error.message}`, "error");
    }
}

function renderHistoryLoading() {
    const holder = document.createElement("div");
    holder.className = "history-empty";
    holder.innerHTML = '<div><span class="loading-ring"></span><p>正在加载对话记录…</p></div>';
    elements.messages.replaceChildren(holder);
}

function renderHistory(messages) {
    elements.messages.replaceChildren();
    if (messages.length === 0) {
        const empty = document.createElement("div");
        empty.className = "history-empty";
        empty.innerHTML = "<div><strong>会话已经准备好</strong><span>在下方输入第一条消息，开始交流。</span></div>";
        elements.messages.append(empty);
        return;
    }

    const fragment = document.createDocumentFragment();
    messages.forEach((message) => {
        const rendered = buildMessageElement({
            role: normalizeRole(message.role),
            content: String(message.content || ""),
            timestamp: Number(message.timestamp || 0),
        });
        fragment.append(rendered.row);
    });
    elements.messages.append(fragment);
    scrollMessagesToBottom(false);
}

function renderHistoryError(message) {
    const holder = document.createElement("div");
    holder.className = "history-empty";
    const wrapper = document.createElement("div");
    const strong = document.createElement("strong");
    strong.textContent = "历史消息加载失败";
    const copy = document.createElement("span");
    copy.textContent = message;
    wrapper.append(strong, copy);
    holder.append(wrapper);
    elements.messages.replaceChildren(holder);
}

async function openModelDialog() {
    if (state.isSending) {
        showToast("请等待当前回复完成后再创建新会话。", "error");
        return;
    }

    elements.confirmModelButton.disabled = true;
    elements.modelForm.reset();
    renderModelLoading();
    if (!elements.modelDialog.open) {
        elements.modelDialog.showModal();
    }

    try {
        const response = await apiJson(API.models);
        state.models = Array.isArray(response.data) ? response.data : [];
        renderModels();
    } catch (error) {
        renderModelError(error.message);
    }
}

function closeModelDialog() {
    if (state.isCreatingSession) {
        return;
    }
    if (elements.modelDialog.open) {
        elements.modelDialog.close();
    }
    elements.modelForm.reset();
    elements.confirmModelButton.disabled = true;
}

function renderModelLoading() {
    const loading = document.createElement("div");
    loading.className = "model-loading";
    loading.innerHTML = '<span class="loading-ring"></span><p>正在获取可用模型…</p>';
    elements.modelGrid.replaceChildren(loading);
}

function renderModelError(message) {
    const error = document.createElement("div");
    error.className = "model-error";
    const copy = document.createElement("p");
    copy.textContent = `模型列表加载失败：${message}`;
    const retry = document.createElement("button");
    retry.type = "button";
    retry.className = "secondary-button";
    retry.textContent = "重新加载";
    retry.addEventListener("click", openModelDialog, { once: true });
    error.append(copy, retry);
    elements.modelGrid.replaceChildren(error);
}

function renderModels() {
    const fragment = document.createDocumentFragment();

    if (state.models.length === 0) {
        renderModelError("服务端暂未返回可用模型");
        return;
    }

    state.models.forEach((model, index) => {
        const name = String(model.name || "");
        const description = String(model.desc || "暂无模型描述");
        const label = document.createElement("label");
        label.className = "model-option";

        const radio = document.createElement("input");
        radio.type = "radio";
        radio.name = "model";
        radio.value = name;
        radio.id = `model-option-${index}`;

        const card = document.createElement("span");
        card.className = "model-option-card";

        const glyph = document.createElement("span");
        glyph.className = "model-glyph";
        glyph.textContent = getModelGlyph(name);

        const copy = document.createElement("span");
        copy.className = "model-copy";
        const modelName = document.createElement("span");
        modelName.className = "model-name";
        modelName.textContent = name;
        const modelDescription = document.createElement("span");
        modelDescription.className = "model-description";
        modelDescription.textContent = description;
        copy.append(modelName, modelDescription);

        const mark = document.createElement("span");
        mark.className = "radio-mark";
        mark.setAttribute("aria-hidden", "true");

        card.append(glyph, copy, mark);
        label.append(radio, card);
        fragment.append(label);
    });

    elements.modelGrid.replaceChildren(fragment);
}

function getModelGlyph(name) {
    const normalized = name.toLowerCase();
    if (normalized.includes("deepseek")) return "DS";
    if (normalized.includes("chatgpt") || normalized.includes("gpt")) return "GPT";
    if (normalized.includes("gemini")) return "GE";
    if (normalized.includes("ollama")) return "OL";
    return name.replace(/[^a-zA-Z0-9]/g, "").slice(0, 2).toUpperCase() || "AI";
}

function handleModelSelection() {
    elements.confirmModelButton.disabled = !elements.modelForm.elements.model?.value;
}

async function createSelectedSession(event) {
    event.preventDefault();
    const modelName = elements.modelForm.elements.model?.value;
    if (!modelName || state.isCreatingSession) {
        return;
    }

    state.isCreatingSession = true;
    elements.confirmModelButton.disabled = true;
    const originalLabel = elements.confirmModelButton.textContent;
    elements.confirmModelButton.textContent = "正在创建…";

    try {
        const response = await apiJson(API.createSession, {
            method: "POST",
            body: JSON.stringify({ model: modelName }),
        });
        const sessionId = String(
            response?.data?.session_id ||
            response?.data?.sessionID ||
            response?.session_id ||
            response?.sessionID ||
            ""
        );

        if (!sessionId) {
            throw new Error("服务端未返回会话 ID");
        }

        const now = Math.floor(Date.now() / 1000);
        const session = normalizeSession({
            id: sessionId,
            model: response?.data?.model || modelName,
            created_at: now,
            updated_at: now,
            message_count: 0,
            first_user_message: "",
        });

        state.sessions = [session, ...state.sessions.filter((item) => item.id !== session.id)];
        closeModelDialogAfterCreate();
        renderSessions();
        await selectSession(session.id);
        elements.messageInput.focus();
        showToast(`已使用 ${session.model} 创建新会话。`);
    } catch (error) {
        showToast(`创建会话失败：${error.message}`, "error");
    } finally {
        state.isCreatingSession = false;
        elements.confirmModelButton.textContent = originalLabel;
        elements.confirmModelButton.disabled = !elements.modelForm.elements.model?.value;
    }
}

function closeModelDialogAfterCreate() {
    if (elements.modelDialog.open) {
        elements.modelDialog.close();
    }
}

async function deleteSession(sessionId) {
    if (state.isSending) {
        showToast("当前会话正在生成回复，暂时不能删除。", "error");
        return;
    }

    const session = state.sessions.find((item) => item.id === sessionId);
    if (!session) {
        return;
    }

    const confirmed = window.confirm(`确定删除会话“${getSessionTitle(session)}”吗？此操作会同步删除服务器数据。`);
    if (!confirmed) {
        return;
    }

    const settleDelay = Math.max(0, state.sessionSettlingUntil - Date.now());
    if (settleDelay > 0 && sessionId === state.activeSessionId) {
        await delay(settleDelay);
    }

    try {
        await apiJson(API.deleteSession(sessionId), { method: "DELETE" });
        state.sessions = state.sessions.filter((item) => item.id !== sessionId);
        if (state.activeSessionId === sessionId) {
            showWelcome();
        }
        renderSessions();
        showToast("会话已删除。 ");
    } catch (error) {
        showToast(`删除会话失败：${error.message}`, "error");
    }
}

function handleComposerInput() {
    const characters = Array.from(elements.messageInput.value);
    if (characters.length > MAX_MESSAGE_LENGTH) {
        elements.messageInput.value = characters.slice(0, MAX_MESSAGE_LENGTH).join("");
    }
    resizeComposer();
    updateComposerState();
}

function handleComposerKeydown(event) {
    if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
        event.preventDefault();
        if (!elements.sendButton.disabled) {
            elements.messageForm.requestSubmit();
        }
    }
}

function resizeComposer() {
    elements.messageInput.style.height = "auto";
    elements.messageInput.style.height = `${Math.min(elements.messageInput.scrollHeight, 156)}px`;
}

function updateComposerState() {
    const length = Array.from(elements.messageInput.value).length;
    elements.characterCount.textContent = `${length}/${MAX_MESSAGE_LENGTH}`;
    elements.characterCount.classList.toggle("is-near-limit", length >= 1800);
    elements.sendButton.disabled = state.isSending || !state.activeSessionId || length === 0;
    elements.messageInput.disabled = state.isSending || !state.activeSessionId;
    elements.sendButton.classList.toggle("is-sending", state.isSending);
}

async function sendMessage(event) {
    event.preventDefault();
    const message = elements.messageInput.value.trim();
    if (!message || !state.activeSessionId || state.isSending) {
        return;
    }

    const sessionId = state.activeSessionId;
    clearHistoryPlaceholder();
    appendMessage({ role: "user", content: message, timestamp: Date.now() / 1000 });
    const assistant = appendMessage({
        role: "assistant",
        content: "",
        timestamp: Date.now() / 1000,
    }, { streaming: true });

    elements.messageInput.value = "";
    resizeComposer();
    state.isSending = true;
    state.streamAbortController = new AbortController();
    updateComposerState();
    scrollMessagesToBottom(true);

    let fullContent = "";
    let renderScheduled = false;

    const renderStream = () => {
        renderScheduled = false;
        assistant.content.innerHTML = fullContent
            ? renderMarkdown(fullContent)
            : buildThinkingPlaceholder();
        scrollMessagesToBottom(true);
    };

    const scheduleStreamRender = () => {
        if (renderScheduled) return;
        renderScheduled = true;
        window.requestAnimationFrame(renderStream);
    };

    try {
        await consumeMessageStream(
            sessionId,
            message,
            (chunk) => {
                fullContent += chunk;
                scheduleStreamRender();
            },
            state.streamAbortController.signal
        );

        if (renderScheduled) {
            renderStream();
        }
        if (!fullContent) {
            throw new Error("模型没有返回有效内容");
        }

        assistant.streaming.hidden = true;
        assistant.time.dateTime = new Date().toISOString();
        assistant.time.textContent = formatMessageTime(Date.now() / 1000);
        state.sessionSettlingUntil = Date.now() + 1200;

        const activeSession = state.sessions.find((item) => item.id === sessionId);
        if (activeSession) {
            activeSession.updated_at = Math.floor(Date.now() / 1000);
            activeSession.message_count += 2;
            if (!activeSession.first_user_message) {
                activeSession.first_user_message = message;
            }
            state.sessions.sort(sortSessionsByUpdatedTime);
            renderSessions();
            updateWorkspaceHeading(activeSession);
        }

        window.setTimeout(() => loadSessions({ silent: true }), 900);
    } catch (error) {
        assistant.streaming.hidden = true;
        if (error.name === "AbortError") {
            assistant.content.innerHTML = renderMarkdown(fullContent || "回复已停止。 ");
        } else {
            assistant.content.innerHTML = renderMarkdown(fullContent || `**回复失败：** ${error.message}`);
            showToast(`消息发送失败：${error.message}`, "error");
        }
    } finally {
        state.isSending = false;
        state.streamAbortController = null;
        updateComposerState();
        elements.messageInput.focus();
        scrollMessagesToBottom(true);
    }
}

async function consumeMessageStream(sessionId, message, onChunk, signal) {
    const response = await fetch(API.streamMessage, {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
            "Accept": "text/event-stream",
        },
        body: JSON.stringify({ session_id: sessionId, message }),
        signal,
    });

    if (!response.ok) {
        const errorText = await response.text();
        let errorMessage = errorText || `HTTP ${response.status}`;
        try {
            errorMessage = JSON.parse(errorText)?.message || errorMessage;
        } catch {
            // Keep the raw response text when the server did not return JSON.
        }
        throw new Error(errorMessage);
    }

    if (!response.body) {
        throw new Error("当前浏览器不支持流式响应读取");
    }

    const reader = response.body.getReader();
    const decoder = new TextDecoder("utf-8");
    let buffer = "";
    let streamFinished = false;

    while (!streamFinished) {
        const { value, done } = await reader.read();
        buffer += decoder.decode(value || new Uint8Array(), { stream: !done });
        buffer = buffer.replace(/\r\n/g, "\n");

        let boundaryIndex = buffer.indexOf("\n\n");
        while (boundaryIndex !== -1) {
            const eventBlock = buffer.slice(0, boundaryIndex);
            buffer = buffer.slice(boundaryIndex + 2);
            const eventResult = processSseEvent(eventBlock, onChunk);
            if (eventResult === "done") {
                streamFinished = true;
                break;
            }
            boundaryIndex = buffer.indexOf("\n\n");
        }

        if (done) {
            if (buffer.trim()) {
                processSseEvent(buffer, onChunk);
            }
            break;
        }
    }

    reader.releaseLock();
}

function processSseEvent(eventBlock, onChunk) {
    const dataLines = eventBlock
        .split("\n")
        .filter((line) => line.startsWith("data:"))
        .map((line) => line.slice(5).replace(/^\s/, ""));

    if (dataLines.length === 0) {
        return "continue";
    }

    const payload = dataLines.join("\n");
    if (payload.trim() === "[DONE]") {
        return "done";
    }

    const content = extractSseContent(payload);
    if (content) {
        onChunk(content);
    }
    return "continue";
}

function extractSseContent(payload) {
    if (!payload) return "";

    try {
        const parsed = JSON.parse(payload);
        if (typeof parsed === "string") return parsed;
        if (typeof parsed?.content === "string") return parsed.content;
        if (typeof parsed?.data === "string") return parsed.data;
        if (typeof parsed?.choices?.[0]?.delta?.content === "string") {
            return parsed.choices[0].delta.content;
        }
        if (typeof parsed?.message?.content === "string") {
            return parsed.message.content;
        }
        return "";
    } catch {
        return payload;
    }
}

function clearHistoryPlaceholder() {
    const placeholder = elements.messages.querySelector(".history-empty");
    if (placeholder) {
        elements.messages.replaceChildren();
    }
}

function appendMessage(message, options = {}) {
    const rendered = buildMessageElement(message, options);
    elements.messages.append(rendered.row);
    return rendered;
}

function buildMessageElement(message, { streaming = false } = {}) {
    const role = normalizeRole(message.role);
    const fragment = elements.messageTemplate.content.cloneNode(true);
    const row = fragment.querySelector(".message-row");
    const content = fragment.querySelector(".message-content");
    const author = fragment.querySelector(".message-author");
    const time = fragment.querySelector(".message-time");
    const streamingStatus = fragment.querySelector(".streaming-status");

    row.classList.add(role === "user" ? "message-row--user" : "message-row--assistant");
    author.textContent = role === "user" ? "您" : (state.activeModel || "AI 助手");
    content.innerHTML = message.content ? renderMarkdown(message.content) : buildThinkingPlaceholder();
    time.dateTime = toIsoDate(message.timestamp);
    time.textContent = formatMessageTime(message.timestamp);
    streamingStatus.hidden = !streaming;

    return { row, content, author, time, streaming: streamingStatus };
}

function normalizeRole(role) {
    return String(role || "assistant").toLowerCase() === "user" ? "user" : "assistant";
}

function buildThinkingPlaceholder() {
    return '<span class="thinking-placeholder" aria-label="模型正在思考"><span></span><span></span><span></span>正在思考</span>';
}

function renderMarkdown(markdown) {
    const lines = String(markdown || "").replace(/\r\n?/g, "\n").split("\n");
    const output = [];
    let index = 0;

    while (index < lines.length) {
        const line = lines[index];

        if (/^\s*```/.test(line)) {
            const language = line.replace(/^\s*```/, "").trim().toLowerCase();
            const codeLines = [];
            index += 1;
            while (index < lines.length && !/^\s*```\s*$/.test(lines[index])) {
                codeLines.push(lines[index]);
                index += 1;
            }
            if (index < lines.length) index += 1;
            const code = codeLines.join("\n");
            output.push(buildCodeBlock(code, language));
            continue;
        }

        if (!line.trim()) {
            index += 1;
            continue;
        }

        const heading = line.match(/^(#{1,4})\s+(.+)$/);
        if (heading) {
            const level = heading[1].length;
            output.push(`<h${level}>${renderInline(heading[2])}</h${level}>`);
            index += 1;
            continue;
        }

        if (/^\s*>\s?/.test(line)) {
            const quotes = [];
            while (index < lines.length && /^\s*>\s?/.test(lines[index])) {
                quotes.push(lines[index].replace(/^\s*>\s?/, ""));
                index += 1;
            }
            output.push(`<blockquote>${renderInline(quotes.join("<br>"), true)}</blockquote>`);
            continue;
        }

        const unordered = line.match(/^\s*[-*+]\s+(.+)$/);
        if (unordered) {
            const items = [];
            while (index < lines.length) {
                const match = lines[index].match(/^\s*[-*+]\s+(.+)$/);
                if (!match) break;
                items.push(`<li>${renderInline(match[1])}</li>`);
                index += 1;
            }
            output.push(`<ul>${items.join("")}</ul>`);
            continue;
        }

        const ordered = line.match(/^\s*\d+[.)]\s+(.+)$/);
        if (ordered) {
            const items = [];
            while (index < lines.length) {
                const match = lines[index].match(/^\s*\d+[.)]\s+(.+)$/);
                if (!match) break;
                items.push(`<li>${renderInline(match[1])}</li>`);
                index += 1;
            }
            output.push(`<ol>${items.join("")}</ol>`);
            continue;
        }

        if (/^\s*(?:---+|___+|\*\*\*+)\s*$/.test(line)) {
            output.push("<hr>");
            index += 1;
            continue;
        }

        const paragraph = [line];
        index += 1;
        while (index < lines.length && lines[index].trim() && !isMarkdownBlockStart(lines[index])) {
            paragraph.push(lines[index]);
            index += 1;
        }
        output.push(`<p>${paragraph.map((item) => renderInline(item)).join("<br>")}</p>`);
    }

    return output.join("");
}

function isMarkdownBlockStart(line) {
    return /^\s*```/.test(line) ||
        /^(#{1,4})\s+/.test(line) ||
        /^\s*>\s?/.test(line) ||
        /^\s*[-*+]\s+/.test(line) ||
        /^\s*\d+[.)]\s+/.test(line) ||
        /^\s*(?:---+|___+|\*\*\*+)\s*$/.test(line);
}

function renderInline(input, allowBreakTag = false) {
    const codeTokens = [];
    const source = String(input).replace(/`([^`\n]+)`/g, (_, code) => {
        const token = `\uE000${codeTokens.length}\uE001`;
        codeTokens.push(code);
        return token;
    });

    let safe = escapeHtml(source);
    if (allowBreakTag) {
        safe = safe.replace(/&lt;br&gt;/g, "<br>");
    }
    safe = safe.replace(/\[([^\]]+)]\((https?:\/\/[^\s)]+)\)/g, '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>');
    safe = safe.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
    safe = safe.replace(/__([^_]+)__/g, "<strong>$1</strong>");
    safe = safe.replace(/(?<!\*)\*([^*\n]+)\*(?!\*)/g, "<em>$1</em>");
    safe = safe.replace(/~~([^~]+)~~/g, "<del>$1</del>");
    safe = safe.replace(/\uE000(\d+)\uE001/g, (_, tokenIndex) => {
        return `<code>${escapeHtml(codeTokens[Number(tokenIndex)] || "")}</code>`;
    });
    return safe;
}

function buildCodeBlock(code, language) {
    const normalizedLanguage = normalizeLanguage(language);
    const label = escapeHtml(normalizedLanguage || "text");
    const highlighted = highlightCode(code, normalizedLanguage);
    return `
        <section class="code-card">
            <div class="code-card-header">
                <span>${label}</span>
                <button class="copy-code-button" type="button" data-action="copy-code">复制代码</button>
            </div>
            <pre><code class="language-${label}">${highlighted}</code></pre>
        </section>`;
}

function normalizeLanguage(language) {
    const aliases = {
        "c++": "cpp",
        "cxx": "cpp",
        "js": "javascript",
        "ts": "typescript",
        "py": "python",
        "sh": "bash",
        "shell": "bash",
        "yml": "yaml",
    };
    const clean = String(language || "text").replace(/[^a-zA-Z0-9_+#.-]/g, "").toLowerCase();
    return aliases[clean] || clean || "text";
}

const LANGUAGE_KEYWORDS = {
    cpp: new Set("alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend if inline int long namespace new noexcept nullptr operator private protected public register reinterpret_cast return short signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while".split(" ")),
    javascript: new Set("async await break case catch class const continue debugger default delete do else export extends finally for from function get if import in instanceof let new of return set static super switch this throw try typeof var void while with yield".split(" ")),
    typescript: new Set("abstract any as asserts async await boolean break case catch class const constructor continue declare default delete do else enum export extends finally for from function get if implements import in infer interface instanceof is keyof let namespace never new null number object of private protected public readonly return set static string super switch symbol this throw true try type typeof undefined unique unknown var void while with yield".split(" ")),
    python: new Set("and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield".split(" ")),
    bash: new Set("case do done elif else esac fi for function if in select then time until while".split(" ")),
    json: new Set(),
    yaml: new Set(),
};

function highlightCode(code, language) {
    const keywords = LANGUAGE_KEYWORDS[language] || new Set();
    const hashComments = language === "python" || language === "bash" || language === "yaml";
    const tokenPattern = hashComments
        ? /#[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`|\b\d+(?:\.\d+)?\b|\b[A-Za-z_$][\w$]*\b/g
        : /\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`|\b\d+(?:\.\d+)?\b|\b[A-Za-z_$][\w$]*\b/g;

    let output = "";
    let lastIndex = 0;
    let match;

    while ((match = tokenPattern.exec(code)) !== null) {
        output += escapeHtml(code.slice(lastIndex, match.index));
        const token = match[0];
        let tokenClass = "";

        if (token.startsWith("//") || token.startsWith("/*") || (hashComments && token.startsWith("#"))) {
            tokenClass = "token-comment";
        } else if (/^["'`]/.test(token)) {
            tokenClass = "token-string";
        } else if (/^\d/.test(token)) {
            tokenClass = "token-number";
        } else if (keywords.has(token)) {
            tokenClass = "token-keyword";
        } else if (["true", "false", "null", "nullptr", "None", "True", "False", "undefined"].includes(token)) {
            tokenClass = "token-literal";
        } else if (/^\s*\(/.test(code.slice(tokenPattern.lastIndex))) {
            tokenClass = "token-function";
        }

        const escapedToken = escapeHtml(token);
        output += tokenClass ? `<span class="${tokenClass}">${escapedToken}</span>` : escapedToken;
        lastIndex = tokenPattern.lastIndex;
    }

    output += escapeHtml(code.slice(lastIndex));
    return output;
}

function escapeHtml(value) {
    return String(value)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}

async function handleMessageAreaClick(event) {
    const copyButton = event.target.closest('[data-action="copy-code"]');
    if (!copyButton) {
        return;
    }

    const code = copyButton.closest(".code-card")?.querySelector("code")?.textContent || "";
    if (!code) return;

    try {
        await copyText(code);
        const originalLabel = copyButton.textContent;
        copyButton.textContent = "已复制";
        window.setTimeout(() => {
            copyButton.textContent = originalLabel;
        }, 1300);
    } catch {
        showToast("复制失败，请手动选择代码。", "error");
    }
}

async function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(text);
        return;
    }

    const textarea = document.createElement("textarea");
    textarea.value = text;
    textarea.style.position = "fixed";
    textarea.style.opacity = "0";
    document.body.append(textarea);
    textarea.select();
    const copied = document.execCommand("copy");
    textarea.remove();
    if (!copied) throw new Error("copy failed");
}

function showWelcome() {
    state.activeSessionId = null;
    state.activeModel = null;
    state.historyRequestToken += 1;
    elements.welcomeView.hidden = false;
    elements.chatView.hidden = true;
    elements.currentSessionTitle.textContent = "新的探索";
    elements.currentModelBadge.hidden = true;
    elements.messages.replaceChildren();
    renderSessions();
    updateComposerState();
    closeSidebar();
}

function showChat() {
    elements.welcomeView.hidden = true;
    elements.chatView.hidden = false;
    updateComposerState();
}

function updateWorkspaceHeading(session) {
    elements.currentSessionTitle.textContent = getSessionTitle(session);
    elements.currentSessionTitle.title = getSessionTitle(session);
    elements.currentModelBadge.textContent = session.model;
    elements.currentModelBadge.hidden = false;
}

function setServiceStatus(status, message) {
    elements.serviceState.classList.toggle("is-online", status === "online");
    elements.serviceState.classList.toggle("is-offline", status === "offline");
    elements.serviceStateText.textContent = message;
}

function closeSidebar() {
    document.body.classList.remove("sidebar-open");
}

function scrollMessagesToBottom(smooth = true) {
    window.requestAnimationFrame(() => {
        elements.messagesViewport.scrollTo({
            top: elements.messagesViewport.scrollHeight,
            behavior: smooth && !window.matchMedia("(prefers-reduced-motion: reduce)").matches ? "smooth" : "auto",
        });
    });
}

function formatSessionTime(timestamp) {
    const date = toDate(timestamp);
    if (!date) return "刚刚";
    const now = new Date();
    const delta = now.getTime() - date.getTime();
    if (delta >= 0 && delta < 60_000) return "刚刚更新";
    if (delta >= 0 && delta < 3_600_000) return `${Math.max(1, Math.floor(delta / 60_000))} 分钟前`;

    const sameDay = date.toDateString() === now.toDateString();
    if (sameDay) {
        return `今天 ${date.toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit", hour12: false })}`;
    }

    return date.toLocaleDateString("zh-CN", {
        month: "2-digit",
        day: "2-digit",
        hour: "2-digit",
        minute: "2-digit",
        hour12: false,
    });
}

function formatMessageTime(timestamp) {
    const date = toDate(timestamp) || new Date();
    return date.toLocaleString("zh-CN", {
        month: "2-digit",
        day: "2-digit",
        hour: "2-digit",
        minute: "2-digit",
        hour12: false,
    });
}

function toDate(timestamp) {
    const numeric = Number(timestamp);
    if (!Number.isFinite(numeric) || numeric <= 0) return null;
    return new Date(numeric > 10_000_000_000 ? numeric : numeric * 1000);
}

function toIsoDate(timestamp) {
    return (toDate(timestamp) || new Date()).toISOString();
}

function showToast(message, type = "success") {
    const toast = document.createElement("div");
    toast.className = `toast${type === "error" ? " toast--error" : ""}`;
    toast.setAttribute("role", type === "error" ? "alert" : "status");
    const copy = document.createElement("p");
    copy.textContent = message;
    toast.append(copy);
    elements.toastRegion.append(toast);

    window.setTimeout(() => {
        toast.style.opacity = "0";
        toast.style.transform = "translateX(14px)";
        window.setTimeout(() => toast.remove(), 180);
    }, 3600);
}

function delay(milliseconds) {
    return new Promise((resolve) => window.setTimeout(resolve, milliseconds));
}
