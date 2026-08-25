#include <ai_chat_sdk/version.h>

#include <iostream>

int main() {
    constexpr std::string_view expected_version = "0.1.0";
    const std::string_view actual_version = ai_chat_sdk::version();

    if (actual_version != expected_version) {
        std::cerr << "expected version " << expected_version
                  << ", got " << actual_version << '\n';
        return 1;
    }

    return 0;
}
