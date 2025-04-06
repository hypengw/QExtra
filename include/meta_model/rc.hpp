#pragma once

#include <cstdint>

namespace meta_model
{
template<typename T>
class Rc {
public:
    Rc(): m_inner(nullptr) {}
    ~Rc() {
        if (m_inner) {
            if (m_inner->decrease() == 0) {
                delete m_inner;
            }
        }
    }
    Rc(const Rc& o) noexcept: m_inner(o.m_inner) { m_inner->increase(); }
    Rc(Rc& o) noexcept: m_inner(o.m_inner) { o.m_inner = nullptr; }
    Rc& operator=(const Rc& o) noexcept {
        m_inner = o.m_inner;
        m_inner->increase();
        return *this;
    }
    Rc& operator=(Rc& o) noexcept {
        m_inner   = o.m_inner;
        o.m_inner = nullptr;
        return *this;
    }

    static auto create(T* t) {
        auto rc    = Rc {};
        rc.m_inner = new Inner { .ptr = t, .count = 1 };
        return rc;
    }

    operator bool() const noexcept { return m_inner != nullptr; }
    T*             operator->() const noexcept { return m_inner->ptr; }
    constexpr bool operator==(const Rc& o) const { return m_inner == o.m_inner; }

private:
    struct Inner {
        T*          ptr;
        std::size_t count;
        void        increase() noexcept { ++count; }
        auto        decrease() noexcept {
            --count;
            return count;
        }
    };
    Inner* m_inner;
};
} // namespace meta_model