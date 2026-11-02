#include "lsw/base/repeat_collapser.hpp"

namespace lsw::base {

RepeatCollapser::Note RepeatCollapser::take_note() {
    Note note{level_, source_, suppressed_};
    suppressed_ = 0;
    return note;
}

RepeatCollapser::Verdict RepeatCollapser::feed(int level, std::string_view source,
                                               std::string_view text, std::int64_t now_ms) {
    Verdict verdict;
    const bool same = level == level_ && source == source_ && text == text_ &&
                      now_ms - last_ms_ <= kGapMs;
    if (same) {
        last_ms_ = now_ms;
        if (suppressed_ > 0 && now_ms - run_start_ms_ >= kWindowMs) {
            verdict.note = take_note();
            run_start_ms_ = now_ms;
        } else if (suppressed_ == 0) {
            run_start_ms_ = now_ms;
        }
        ++suppressed_;
        verdict.write = false;
        return verdict;
    }
    if (suppressed_ > 0) {
        verdict.note = take_note();
    }
    level_ = level;
    source_.assign(source);
    text_.assign(text);
    run_start_ms_ = now_ms;
    last_ms_ = now_ms;
    return verdict;
}

std::optional<RepeatCollapser::Note> RepeatCollapser::due(std::int64_t now_ms) {
    if (suppressed_ > 0 && now_ms - run_start_ms_ >= kWindowMs) {
        run_start_ms_ = now_ms;
        return take_note();
    }
    return std::nullopt;
}

}
