#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lsw::base {
class RepeatCollapser {
public:
    static constexpr std::int64_t kWindowMs = 5000;
    static constexpr std::int64_t kGapMs = 2000;

    struct Note {
        int level = 0;
        std::string source;
        std::uint32_t count = 0;
    };

    struct Verdict {
        std::optional<Note> note;
        bool write = true;
    };

    [[nodiscard]] Verdict feed(int level, std::string_view source, std::string_view text,
                               std::int64_t now_ms);

    [[nodiscard]] std::optional<Note> due(std::int64_t now_ms);

private:
    [[nodiscard]] Note take_note();

    int level_ = -1;
    std::string source_;
    std::string text_;
    std::int64_t run_start_ms_ = 0;
    std::int64_t last_ms_ = 0;
    std::uint32_t suppressed_ = 0;
};

}
