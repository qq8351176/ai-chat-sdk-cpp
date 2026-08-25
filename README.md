# ai-chat-sdk-cpp

A C++17 SDK that puts DeepSeek, ChatGPT, Gemini and local Ollama models behind one
interface, plus an HTTP chat server built on it — SSE streaming, SQLite-backed
sessions, and a small web frontend.

> **Status:** personal project, `v0.1.0`. The SDK builds and installs as a proper
> CMake package; the server is a working single-process deployment.

---

## What's in here

| Layer | Path | What it does |
|---|---|---|
| **SDK** | `include/`, `src/` | Static library `ai_chat_sdk`. Provider abstraction, session management, SQLite persistence. |
| **Server** | `apps/chat_server/` | `AIChatServer` — cpp-httplib REST API + SSE, gflags config, static web UI. |
| **Tests** | `tests/` | CTest smoke tests, plus a gtest integration harness. |

### Design

```
                  ┌──────────────────────────────────────┐
   HTTP / SSE ──▶ │  AIChatServer   (cpp-httplib, gflags) │
                  └───────────────────┬──────────────────┘
                                      │
                  ┌───────────────────▼──────────────────┐
                  │  ChatSDK                             │
                  │    ├── SessionManager ── DataManager ─┼──▶ SQLite (chatDB.db)
                  │    └── LLMManager                    │
                  └───────────────────┬──────────────────┘
                                      │  LLMProvider (abstract)
              ┌───────────┬───────────┼───────────┬───────────────┐
              ▼           ▼           ▼           ▼               ▼
        DeepSeek     ChatGPT       Gemini      Ollama      (add your own)
```

`LLMProvider` is the extension point: each provider owns its own request/response
JSON shape, so `LLMManager` never parses vendor-specific payloads. Adding a model
means implementing one interface and registering it — no changes to the manager,
the session layer, or the server.

Both a blocking (`sendMessage`) and an incremental (`sendMessageStream`) path are
implemented for every provider; the server maps the latter onto SSE.

---

## Build

### Prerequisites

| Dependency | Used by | Debian/Ubuntu |
|---|---|---|
| CMake ≥ 3.16, a C++17 compiler | everything | `build-essential cmake` |
| [spdlog](https://github.com/gabime/spdlog) | logging | `libspdlog-dev` |
| [jsoncpp](https://github.com/open-source-parsers/jsoncpp) | JSON | `libjsoncpp-dev` |
| SQLite3 | persistence | `libsqlite3-dev` |
| OpenSSL | HTTPS to model APIs | `libssl-dev` |
| [gflags](https://github.com/gflags/gflags) | server config | `libgflags-dev` |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | HTTP client/server | header-only, see below |
| GoogleTest *(optional)* | integration harness | `libgtest-dev` |

### cpp-httplib

`httplib.h` is header-only and **is not vendored** in this repository. Drop it into
`third_party/httplib/` before configuring, or install it system-wide:

```bash
mkdir -p third_party/httplib
curl -L -o third_party/httplib/httplib.h \
  https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
```

Without it CMake stops with
`httplib.h was not found; install cpp-httplib or place it in third_party/httplib`.

### Configure and build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Useful options:

| Option | Default | Effect |
|---|---|---|
| `AI_CHAT_SDK_BUILD_SERVER` | `ON` | Build `AIChatServer`. Turn off for an SDK-only build. |
| `AI_CHAT_SDK_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX`. |
| `BUILD_TESTING` | `ON` | CTest smoke tests. |

### Install and consume

```bash
cmake --install build --prefix /your/prefix
```

The install exports a relocatable CMake package, so downstream projects use it the
normal way:

```cmake
find_package(ai_chat_sdk 0.1 REQUIRED)
target_link_libraries(your_app PRIVATE ai_chat_sdk::ai_chat_sdk)
```

---

## Run the server

API keys come from the **environment**, never from the config file or the repo:

```bash
cp .env.example .env     # fill in the keys you have
set -a; source .env; set +a

cd build && ./AIChatServer --config_file=./ChatServer.conf
```

Then open <http://localhost:8080>.

Every provider is optional — start with only `DEEPSEEK_API_KEY`, or with none at all
and a local Ollama instance.

### Configuration

`apps/chat_server/ChatServer.conf` is a gflags file. Precedence is
**defaults < config file < command line**.

| Flag | Default | Meaning |
|---|---|---|
| `--host` | `0.0.0.0` | Bind address |
| `--port` | `8080` | Listen port |
| `--log_level` | `INFO` | `TRACE`/`DEBUG`/`INFO`/`WARN`/`ERROR`/`CRITICAL` |
| `--temperature` | `0.7` | Sampling temperature, 0.0–2.0 |
| `--max_tokens` | `2048` | Max generated tokens |
| `--ollama_model_name` | — | e.g. `deepseek-r1:1.5b` |
| `--ollama_endpoint` | — | e.g. `http://127.0.0.1:11434` |

| Environment variable | Fallback |
|---|---|
| `DEEPSEEK_API_KEY` | — |
| `CHATGPT_API_KEY` | `OPENAI_API_KEY`, then `DEEPSEEK_API_KEY` |
| `GEMINI_API_KEY` | — |

Sessions and message history persist to `chatDB.db` in the working directory.

---

## HTTP API

Full spec: [`apps/chat_server/AIChatServer.openapi.yaml`](apps/chat_server/AIChatServer.openapi.yaml)

| Method | Path | Purpose |
|---|---|---|
| `POST` | `/api/session` | Create a session for a model |
| `GET` | `/api/sessions` | List sessions |
| `GET` | `/api/models` | List available models |
| `DELETE` | `/api/session/{session_id}` | Delete a session |
| `GET` | `/api/session/{session_id}/history` | Session message history |
| `POST` | `/api/message` | Send a message, full response |
| `POST` | `/api/message/async` | Send a message, **SSE stream** |

```bash
SESSION=$(curl -s -X POST localhost:8080/api/session \
  -H 'Content-Type: application/json' \
  -d '{"model_name":"deepseek-chat"}' | jq -r .session_id)

curl -N -X POST localhost:8080/api/message/async \
  -H 'Content-Type: application/json' \
  -d "{\"session_id\":\"$SESSION\",\"message\":\"Explain RAII in two sentences.\"}"
```

---

## Using the SDK directly

```cpp
#include <ai_chat_sdk/ChatSDK.h>

using namespace ai_chat_sdk;

const char* key = std::getenv("DEEPSEEK_API_KEY");

auto deepseek = std::make_shared<APIConfig>();
deepseek->_modelName   = "deepseek-chat";   // "chatgpt" | "gemini"
deepseek->_apiKey      = key ? key : "";
deepseek->_temperature = 0.7;
deepseek->_maxTokens   = 2048;

ChatSDK sdk;
sdk.initModels({deepseek});

const std::string session = sdk.createSession("deepseek-chat");

// Blocking
std::string reply = sdk.sendMessage(session, "Hello!");

// Streaming — callback receives (delta, isLast)
sdk.sendMessageStream(session, "Tell me a story.",
    [](const std::string& delta, bool done) {
        std::cout << delta << std::flush;
        if (done) std::cout << '\n';
    });
```

For a local Ollama model, use `OllamaConfig` with `_modelDesc` and `_endpoint`
instead of an API key.

---

## Tests

```bash
ctest --test-dir build --output-on-failure   # smoke tests, no keys or network
```

`tests/testLLM.cpp` is a GoogleTest harness that talks to the real provider APIs. It
is built by the standalone `tests/CMakeLists.txt` rather than the top-level build,
and needs API keys plus network access:

```bash
cmake -S tests -B tests/build && cmake --build tests/build && ./tests/build/testLLM
```

---

## Known limitations

- Single process, no auth, no rate limiting — this is not a production gateway.
- The four provider tests in `tests/testLLM.cpp` are behind `#if 0`.
- Source comments are in Chinese.

## License

[MIT](LICENSE)
