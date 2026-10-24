#pragma once

#include <functional>
#include <utility>

namespace kstore {

class Subscription {
public:
    Subscription() = default;
    explicit Subscription(std::function<void()> release): m_release(std::move(release)) {}
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept
        : m_release(std::exchange(other.m_release, {})) {}
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            reset();
            m_release = std::exchange(other.m_release, {});
        }
        return *this;
    }
    ~Subscription() { reset(); }
    void reset() {
        auto release = std::exchange(m_release, {});
        if (release) release();
    }
    explicit operator bool() const { return bool(m_release); }

private:
    std::function<void()> m_release;
};

} // namespace kstore
