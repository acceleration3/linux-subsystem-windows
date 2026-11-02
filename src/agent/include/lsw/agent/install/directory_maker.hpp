#pragma once

#include <string>

namespace lsw::agent::install {
class DirectoryMaker {
public:
    DirectoryMaker() = delete;

    static void make_path(const std::wstring& path);

    static void remove_tree(const std::wstring& path);
};

}
