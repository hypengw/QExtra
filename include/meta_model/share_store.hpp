#pragma once

#include <span>
#include <functional>
#include <map>
#include <memory>

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QMetaObject>

#include "meta_model/item_trait.hpp"

namespace meta_model
{

///
/// @brief store_type
template<typename T, typename TItem>
concept storeable =
    requires(T t, TItem item, typename ItemTrait<TItem>::key_type key, std::int64_t handle) {
        { t.store_query(key) } -> std::same_as<TItem*>;
        t.store_insert(item, true, handle);
        t.store_remove(key);
        {
            t.store_reg_notify([](std::span<const decltype(key)>) {
            })
        } -> std::same_as<std::int64_t>;
        t.store_unreg_notify(handle);
    };

///
/// @brief Item that defined store_type in ItemTrait
template<typename T>
concept storeable_item = hashable_item<T> && requires(T t) { typename ItemTrait<T>::store_type; } &&
                         storeable<typename ItemTrait<T>::store_type, T>;

template<typename T, typename Allocator = std::allocator<T>, typename InnerCustom = std::int64_t,
         typename TItemExtend = void>
struct ShareStore {
    using handle_type   = std::int64_t;
    using key_type      = typename ItemTrait<T>::key_type;
    using callback_type = std::function<void(std::span<const key_type>)>;

    template<typename U>
    using rebind_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<U>;
    struct _Item {
        T           item;
        handle_type count { 1 };
    };

    struct _ItemEx {
        T           item;
        handle_type count { 1 };
        TItemExtend extend {};
    };

    using item_type = std::conditional_t<std::same_as<void, TItemExtend>, _Item, _ItemEx>;

    struct Inner {
        Inner(Allocator alloc)
            : map(alloc), callbacks(alloc), serial(0), event(new QObject { nullptr }) {}
        ~Inner() { delete event; }

        std::unordered_map<key_type, item_type, std::hash<key_type>, std::equal_to<key_type>,
                           rebind_alloc<std::pair<const key_type, item_type>>>
            map;
        std::map<handle_type, callback_type, std::less<>,
                 rebind_alloc<std::pair<const handle_type, callback_type>>>
                    callbacks;
        handle_type serial;
        QObject*    event;

        InnerCustom custom;

        template<typename U>
        void delay_callback(handle_type req_handle, U&& range) {
            QMetaObject::invokeMethod(event, [this, req_handle, range, p = QPointer(event)] {
                if (! p) return;
                for (auto& el : callbacks) {
                    if (el.first == req_handle) continue;
                    el.second(range);
                }
            });
        }
    };

    std::shared_ptr<Inner> inner;

    ShareStore(Allocator alloc = Allocator {}): inner(std::make_shared<Inner>(alloc)) {}

    Allocator get_allocator() { return inner->map.get_allocator(); }

    auto store_query(param_type<key_type> k) -> T* {
        auto it = inner->map.find(k);
        if (it != inner->map.end()) {
            return std::addressof(it->second.item);
        }
        return nullptr;
    }
    auto store_query(param_type<key_type> k) const -> T const* {
        auto it = inner->map.find(k);
        if (it != inner->map.end()) {
            return std::addressof(it->second.item);
        }
        return nullptr;
    }
    void store_insert(param_type<T> item, bool new_one, handle_type handle) {
        std::vector<key_type, rebind_alloc<key_type>> changed;
        auto                                          key = ItemTrait<T>::key(item);
        if (auto it = inner->map.find(key); it != inner->map.end()) {
            it->second.item = item;
            if (new_one) ++(it->second.count);

            changed.emplace_back(key);
        } else {
            inner->map.insert({ key, item_type { .item = item } });
        }

        if (! changed.empty()) {
            inner->delay_callback(handle, std::move(changed));
        }
    }
    void store_remove(param_type<key_type> k) {
        if (auto it = inner->map.find(k); it != inner->map.end()) {
            auto count = --(it->second.count);
            if (count == 0) inner->map.erase(count);
        }
    }

    auto store_reg_notify(callback_type cb) -> handle_type {
        auto handle = ++(inner->serial);
        inner->callbacks.insert({ handle, cb });
        return handle;
    }
    void store_unreg_notify(handle_type handle) { inner->callbacks.erase(handle); }
};

} // namespace meta_model