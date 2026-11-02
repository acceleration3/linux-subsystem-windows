#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsw::agent::win {
class RunAs;
}

namespace lsw::agent::console {
class Console {
public:
    enum class Kind {
        Output,
        Closed,
    };

    struct Note {
        Kind kind;
        std::string session_id;
        std::vector<std::uint8_t> data;
        int exit_code = 0;
    };

    [[nodiscard]] std::string open(const win::RunAs& as);

    [[nodiscard]] bool write(const std::string& session_id, const std::string& data);

    void close(const std::string& session_id);

    [[nodiscard]] std::vector<Note> collect_pending();

    Console();
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}
