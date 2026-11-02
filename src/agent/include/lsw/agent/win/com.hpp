#pragma once

namespace lsw::agent::win {
class Apartment {
public:
    enum class Model { Multithreaded, SingleThreaded };

    explicit Apartment(Model model = Model::Multithreaded);
    ~Apartment();
    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;

    [[nodiscard]] bool ok() const noexcept { return initialised_; }

private:
    bool initialised_ = false;
};

}
