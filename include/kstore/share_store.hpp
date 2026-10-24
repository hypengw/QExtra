#pragma once

#include <span>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "kstore/item_trait.hpp"
#include "kstore/rc.hpp"
#include "kstore/subscription.hpp"

namespace kstore
{

///
/// @brief store_type
template<typename T, typename TItem>
concept storeable =
    requires(T t, TItem item, typename ItemTrait<TItem>::key_type key, std::int64_t handle) {
        { t.store_query(key) } -> std::same_as<TItem*>;
        t.store_insert(item);
        t.store_remove(key);
        // {
        //     t.store_reg_notify([](std::span<const decltype(key)>) {
        //     })
        // } -> std::same_as<std::int64_t>;
        t.store_unreg_notify(handle);
    };

///
/// @brief Item that defined store_type in ItemTrait
template<typename T>
concept storeable_item = hashable_item<T> && requires(T t) { typename ItemTrait<T>::store_type; } &&
                         storeable<typename ItemTrait<T>::store_type, T>;

template<typename T, typename Allocator = std::allocator<T>, typename TItemExtend = void,
         typename InnerCustom = std::int64_t>
struct ShareStore;

template<typename T, typename Store>
class StoreItem {
    using key_type = typename kstore::ItemTrait<T>::key_type;
    template<typename, typename Allocator, typename TItemExtend, typename InnerCustom>
    friend struct ShareStore;

    StoreItem(Store s, key_type k): m_store(s), m_key(k) { Q_ASSERT(m_key); }

public:
    StoreItem() = delete;
    StoreItem(Store s): m_store(s) {}

    StoreItem(const StoreItem& o): m_store(o.m_store), m_key(o.m_key) { increase(); }
    StoreItem(StoreItem&& o) noexcept: m_store(o.m_store), m_key(o.m_key) { o.m_key = {}; }
    StoreItem& operator=(const StoreItem& o) {
        if (this == &o) return *this;
        release();
        m_store = o.m_store;
        m_key   = o.m_key;
        increase();
        return *this;
    }
    StoreItem& operator=(StoreItem&& o) noexcept {
        if (this == &o) return *this;
        release();
        m_store = o.m_store;
        m_key   = o.m_key;
        o.m_key = {};
        return *this;
    }

    constexpr bool operator==(const StoreItem& o) const {
        return m_key == o.m_key && m_store == o.m_store;
    }

    ~StoreItem() { release(); }

    auto item() const -> T* { return store_query(); }
    auto operator*() const -> T& { return *store_query(); }
    auto operator->() const -> T* { return store_query(); }
         operator bool() const { return store_query() != nullptr; }

    auto key() const { return m_key; }
    auto store() const { return m_store; }

    // increase ref count, careful to call this
    auto increase() {
        if (m_key) m_store.store_increase(*m_key);
    }

private:
    void release() {
        if (m_key) m_store.store_remove(*m_key);
        m_key.reset();
    }
    auto store_query() const -> T* {
        if (m_key) {
            return m_store.store_query(*m_key);
        }
        return nullptr;
    }

    Store                   m_store;
    std::optional<key_type> m_key;
};

template<typename T, typename Allocator, typename TItemExtend, typename InnerCustom>
struct ShareStore {
    using handle_type     = std::int64_t;
    using key_type        = typename ItemTrait<T>::key_type;
    using callback_type   = std::function<void(std::span<const key_type>)>;
    using store_item_type = StoreItem<T, ShareStore>;
    using item_type       = T;

    template<typename U>
    using rebind_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<U>;
    template<typename, typename>
    friend class StoreItem;
    struct _Item {
        _Item(T item, handle_type count): item(item), count(count) {}

        T           item;
        handle_type count;
        auto        increase() noexcept { return ++count; }
        auto        decrease() noexcept { return --count; }
    };

    struct _ItemEx {
        _ItemEx(T item, handle_type count): item(item), count(count), extend() {}
        ~_ItemEx()              = default;
        _ItemEx(const _ItemEx&) = default;
        _ItemEx(_ItemEx&&)      = default;

        T           item;
        handle_type count;
        TItemExtend extend;

        auto increase() noexcept { return ++count; }
        auto decrease() noexcept { return --count; }
    };

    using inner_item_type = std::conditional_t<std::same_as<void, TItemExtend>, _Item, _ItemEx>;

    struct Inner {
        Inner(Allocator alloc): map(alloc), callbacks(alloc), serial(0) {}
        ~Inner() {}

        std::unordered_map<key_type, inner_item_type, std::hash<key_type>, std::equal_to<key_type>,
                           rebind_alloc<std::pair<const key_type, inner_item_type>>>
            map;
        std::map<handle_type, callback_type, std::less<>,
                 rebind_alloc<std::pair<const handle_type, callback_type>>>
                    callbacks;
        handle_type serial;

        InnerCustom custom;
    };

    Rc<Inner> inner;

    ShareStore(Allocator alloc = Allocator {}): inner(Rc<Inner>::create(new Inner(alloc))) {}

    constexpr bool operator==(const ShareStore& o) const { return inner == o.inner; }

    Allocator get_allocator() { return inner->map.get_allocator(); }

    auto store_query(param_type<key_type> k) const -> T* {
        auto it = inner->map.find(k);
        if (it != inner->map.end()) {
            return std::addressof(it->second.item);
        }
        return nullptr;
    }

    template<typename Range>
    void store_changed_callback(const Range& range, std::int64_t ignore_handle = 0) {
        std::vector<handle_type> handles;
        for (auto& [handle, _] : inner->callbacks) handles.push_back(handle);
        for (auto handle : handles) {
            if (handle == ignore_handle) continue;
            if (auto it = inner->callbacks.find(handle); it != inner->callbacks.end()) {
                auto callback = it->second;
                callback(range);
            }
        }
    }

    auto store_insert(param_type<T> item) -> std::pair<store_item_type, bool> {
        bool changed { false };
        auto key = ItemTrait<T>::key(item);
        if (auto it = inner->map.find(key); it != inner->map.end()) {
            if constexpr (requires { item == it->second.item; }) {
                changed = !(it->second.item == item);
            } else {
                changed = true;
            }
            if (changed) it->second.item = item;
            // for store item
            it->second.increase();

        } else {
            inner->map.insert(std::pair { key, inner_item_type { item, 1 } });
            changed = true;
        }

        return { { *this, key }, changed };
    }

    auto store_item(param_type<key_type> k) -> std::optional<store_item_type> {
        if (auto it = inner->map.find(k); it != inner->map.end()) {
            it->second.increase();
            return store_item_type { *this, k };
        }
        return std::nullopt;
    }

    void store_increase(param_type<key_type> k) {
        if (auto it = inner->map.find(k); it != inner->map.end()) {
            it->second.increase();
        }
    }

    void store_remove(param_type<key_type> k) {
        if (auto it = inner->map.find(k); it != inner->map.end()) {
            auto count = it->second.decrease();
            if (count == 0) inner->map.erase(it);
        }
    }

    auto store_reg_notify(callback_type cb) -> handle_type {
        auto handle = ++(inner->serial);
        inner->callbacks.insert({ handle, cb });
        return handle;
    }
    void store_unreg_notify(handle_type handle) { inner->callbacks.erase(handle); }

    auto subscribe(key_type key, callback_type callback) -> Subscription {
        auto handle = store_reg_notify([key, callback = std::move(callback)](auto keys) {
            for (auto changed : keys) {
                if (changed == key) {
                    callback(keys);
                    break;
                }
            }
        });
        return Subscription([store = *this, handle]() mutable { store.store_unreg_notify(handle); });
    }

    // extend
    auto query_extend(kstore::param_type<key_type> key) -> TItemExtend*
        requires(! std::same_as<TItemExtend, void>)
    {
        auto& map = this->inner->map;
        if (auto it = map.find(key); it != map.end()) {
            return std::addressof(it->second.extend);
        }
        return nullptr;
    }

    auto query_extend(kstore::param_type<key_type> key) const -> TItemExtend*
        requires(! std::same_as<TItemExtend, void>)
    {
        auto& map = this->inner->map;
        if (auto it = map.find(key); it != map.end()) {
            return std::addressof(it->second.extend);
        }
        return nullptr;
    }

    auto size() const -> std::size_t { return inner->map.size(); }
};

} // namespace kstore
