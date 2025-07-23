#pragma once

#include <ranges>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <set>

#include <QtCore/QAbstractItemModel>
#include "kstore/item_trait.hpp"
#include "kstore/share_store.hpp"

namespace kstore
{
enum class ListStoreType
{
    Vector = 0,
    VectorWithMap,
    Map,
    Share
};
}

namespace kstore::detail
{

template<typename T, typename Allocator, ListStoreType Store>
class ListImpl;

template<typename T, typename TItem>
concept syncable_list =
    std::ranges::sized_range<T> &&
    std::same_as<std::remove_cvref_t<std::ranges::range_value_t<T>>, TItem> && hashable_item<TItem>;

template<typename T, ListStoreType>
struct allocator_helper {
    using value_type = T;
};
template<typename T>
struct allocator_helper<T, ListStoreType::Map> {
    using value_type = std::pair<const usize, T>;
};

template<typename T, ListStoreType S>
using allocator_value_type = allocator_helper<T, S>::value_type;

template<typename Allocator, typename T>
using rebind_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

template<typename K, typename V, typename Allocator>
using HashMap = std::unordered_map<K, V, std::hash<K>, std::equal_to<K>,
                                   rebind_alloc<Allocator, std::pair<const K, V>>>;

template<typename T, typename Allocator>
using Set = std::set<T, std::less<>, rebind_alloc<Allocator, T>>;

template<typename T, typename Allocator>
class ListImpl<T, Allocator, ListStoreType::Vector> {
public:
    using allocator_type = Allocator;
    using container_type = std::vector<T, Allocator>;
    using iterator       = container_type::iterator;

    ListImpl(Allocator allc = Allocator()): m_items(allc) {}
    auto        begin() const { return std::begin(m_items); }
    auto        end() const { return std::end(m_items); }
    auto        begin() { return std::begin(m_items); }
    auto        end() { return std::end(m_items); }
    auto        size() const { return std::size(m_items); }
    const auto& at(usize idx) const { return m_items.at(idx); }
    auto&       at(usize idx) { return m_items.at(idx); }
    auto        find(param_type<T> t) const { return std::find(begin(), end(), t); }
    auto        find(param_type<T> t) { return std::find(begin(), end(), t); }
    auto        get_allocator() const { return m_items.get_allocator(); }

protected:
    template<std::ranges::sized_range U>
    auto _insert_len(U&& range) {
        return range.size();
    }

    template<std::ranges::range U>
    void _insert_impl(usize idx, U&& range) {
        std::ranges::copy(std::forward<U>(range), std::insert_iterator(m_items, begin() + idx));
    }

    void _erase_impl(usize index, usize last) {
        auto it = m_items.begin();
        m_items.erase(it + index, it + last);
    }

    void _reset_impl() { m_items.clear(); }

    template<std::ranges::range U>
    void _reset_impl(const U& items) {
        m_items.clear();
        _insert_impl(0, items);
    }

    void _move_impl(usize sourceRow, usize destinationRow, usize count) {
        auto it  = m_items.begin();
        auto src = it + sourceRow;
        auto dst = it + destinationRow;
        if (sourceRow > destinationRow) {
            std::rotate(dst, src, src + count);
        } else {
            std::rotate(src, src + count, dst);
        }
    }

private:
    container_type m_items;
};

template<typename T, typename Allocator>
class ListImpl<T, Allocator, ListStoreType::VectorWithMap> {
public:
    using allocator_type = Allocator;
    using container_type = std::vector<T, Allocator>;
    using key_type       = ItemTrait<T>::key_type;
    using iterator       = container_type::iterator;

    ListImpl(Allocator allc = Allocator()): m_map(allc), m_items(allc) {}

    auto        begin() const { return std::begin(m_items); }
    auto        end() const { return std::end(m_items); }
    auto        begin() { return std::begin(m_items); }
    auto        end() { return std::end(m_items); }
    auto        size() const { return std::size(m_items); }
    const auto& at(usize idx) const { return m_items.at(idx); }
    auto&       at(usize idx) { return m_items.at(idx); }
    auto        find(param_type<T> t) const { return std::find(begin(), end(), t); }
    auto        find(param_type<T> t) { return std::find(begin(), end(), t); }
    auto        get_allocator() const { return m_items.get_allocator(); }

    // hash
    auto contains(param_type<T> t) const { return m_map.contains(ItemTrait<T>::key(t)); }
    auto key_at(usize idx) const { return ItemTrait<T>::key(m_items.at(idx)); }
    auto query_idx(param_type<key_type> key) const -> std::optional<usize> {
        if (auto it = m_map.find(key); it != m_map.end()) {
            return it->second;
        }
        return std::nullopt;
    };
    T* query(param_type<key_type> key) {
        auto idx = this->query_idx(key);
        if (idx) return this->at(*idx);
        return nullptr;
    }
    T const* query(param_type<key_type> key) const {
        auto idx = this->query_idx(key);
        if (idx) return this->at(*idx);
        return nullptr;
    }

protected:
    template<std::ranges::sized_range U>
    auto _insert_len(U&& range) {
        auto view = std::views::transform(range, [this](auto& el) -> usize {
            return m_map.contains(ItemTrait<T>::key(el)) ? 0 : 1;
        });
        return std::accumulate(view.begin(), view.end(), 0);
    }

    template<std::ranges::range U>
    void _insert_impl(usize idx, U&& range) {
        std::ranges::copy(std::forward<U>(range), std::insert_iterator(m_items, begin() + idx));
        for (auto i = idx; i < m_items.size(); i++) {
            m_map.insert_or_assign(ItemTrait<T>::key(m_items.at(i)), i);
        }
    }

    void _erase_impl(usize idx, usize last) {
        auto it = m_items.begin();
        for (auto i = idx; i < last; i++) {
            m_map.erase(ItemTrait<T>::key(m_items.at(i)));
        }
        m_items.erase(it + idx, it + last);
    }

    void _reset_impl() {
        m_items.clear();
        m_map.clear();
    }

    template<std::ranges::range U>
    void _reset_impl(U&& items) {
        m_items.clear();
        m_map.clear();
        _insert_impl(0, std::forward<U>(items));
    }

    void _move_impl(usize sourceRow, usize destinationRow, usize count) {
        auto it  = m_items.begin();
        auto src = it + sourceRow;
        auto dst = it + destinationRow;
        if (sourceRow > destinationRow) {
            std::rotate(dst, src, src + count);
            for (auto i = destinationRow; i < sourceRow + count; i++) {
                m_map.insert_or_assign(ItemTrait<T>::key(m_items.at(i)), i);
            }
        } else {
            std::rotate(src, src + count, dst);
            for (auto i = sourceRow; i < destinationRow; i++) {
                m_map.insert_or_assign(ItemTrait<T>::key(m_items.at(i)), i);
            }
        }
    }

    auto& _maps() { return m_map; }

private:
    // indexed cache with hash
    HashMap<key_type, usize, allocator_type> m_map;
    container_type                           m_items;
};
template<typename T, typename Allocator>
class ListImpl<T, Allocator, ListStoreType::Map> {
public:
    using allocator_type = Allocator;
    using key_type       = ItemTrait<T>::key_type;
    using container_type =
        std::unordered_map<key_type, T, std::hash<key_type>, std::equal_to<key_type>,
                           detail::rebind_alloc<Allocator, std::pair<const key_type, T>>>;
    using iterator = container_type::iterator;

    ListImpl(Allocator allc = Allocator()): m_order(allc), m_items(allc) {}

    auto        begin() const { return std::begin(m_items); }
    auto        end() const { return std::end(m_items); }
    auto        begin() { return std::begin(m_items); }
    auto        end() { return std::end(m_items); }
    auto        size() const { return std::size(m_items); }
    const auto& at(usize idx) const { return *query(m_order.at(idx)); }
    auto&       at(usize idx) { return *query(m_order.at(idx)); }
    auto        get_allocator() const { return m_order.get_allocator(); }

    // hash
    auto contains(param_type<T> t) const { return m_items.contains(ItemTrait<T>::key(t)); }
    auto key_at(usize idx) const { return m_order.at(idx); }

    auto query_idx(param_type<key_type> key) const -> std::optional<usize> {
        if (auto it = std::find(m_order.begin(), m_order.end(), key); it != m_order.end()) {
            return std::distance(m_order.begin(), it);
        }
        return std::nullopt;
    };

    T* query(param_type<key_type> key) {
        if (auto it = m_items.find(key); it != m_items.end()) return std::addressof(it->second);
        return nullptr;
    }
    T const* query(param_type<key_type> key) const {
        if (auto it = m_items.find(key); it != m_items.end()) return std::addressof(it->second);
        return nullptr;
    }

protected:
    template<std::ranges::sized_range U>
    auto _insert_len(U&& range) {
        auto view = std::views::transform(range, [this](auto& el) -> usize {
            return m_items.contains(ItemTrait<T>::key(el)) ? 0 : 1;
        });
        return std::accumulate(view.begin(), view.end(), 0);
    }

    template<std::ranges::range U>
    void _insert_impl(usize it, U&& range) {
        std::vector<key_type, detail::rebind_alloc<allocator_type, key_type>> order(
            get_allocator());
        for (auto&& el : std::forward<U>(range)) {
            auto k = ItemTrait<T>::key(el);
            order.emplace_back(k);
            m_items.insert_or_assign(k, std::forward<decltype(el)>(el));
        }
        m_order.insert(m_order.begin() + it, order.begin(), order.end());
    }

    void _erase_impl(usize index, usize last) {
        auto it    = m_order.begin();
        auto begin = it + index;
        auto end   = it + last;
        for (auto it = begin; it != end; it++) {
            m_items.erase(*it);
        }
        m_order.erase(it + index, it + last);
    }

    void _reset_impl() {
        m_items.clear();
        m_order.clear();
    }

    template<std::ranges::range U>
    void _reset_impl(U&& items) {
        m_order.clear();
        m_items.clear();
        _insert_impl(0, std::forward<U>(items));
    }

    void _move_impl(usize sourceRow, usize destinationRow, usize count) {
        auto it  = m_order.begin();
        auto src = it + sourceRow;
        auto dst = it + destinationRow;
        if (sourceRow > destinationRow) {
            std::rotate(dst, src, src + count);
        } else {
            std::rotate(src, src + count, dst);
        }
    }

private:
    std::vector<key_type, detail::rebind_alloc<allocator_type, key_type>> m_order;
    container_type                                                        m_items;
};

template<typename T, typename Allocator>
class ListImpl<T, Allocator, ListStoreType::Share> {
public:
    static_assert(storeable_item<T>);
    using allocator_type = Allocator;
    using key_type       = ItemTrait<T>::key_type;
    using store_type     = ItemTrait<T>::store_type;
    using container_type =
        std::unordered_map<key_type, T, std::hash<key_type>, std::equal_to<key_type>,
                           detail::rebind_alloc<Allocator, std::pair<const key_type, T>>>;
    using iterator = container_type::iterator;

    ListImpl(Allocator allc = Allocator())
        : m_order(allc),
          m_view(std::views::transform(m_order, Trans { this })),
          m_notify_handle(0) {}

    ~ListImpl() {
        m_store->store_unreg_notify(m_notify_handle);
        _reset_impl();
    }

    auto begin() const { return m_view.begin(); }
    auto end() const { return m_view.end(); }
    auto begin() { return m_view.begin(); }
    auto end() { return m_view.end(); }
    auto size() const { return m_order.size(); }

    const auto& at(usize idx) const { return *query(m_order.at(idx)); }
    auto&       at(usize idx) { return *query(m_order.at(idx)); }
    auto        get_allocator() const { return m_order.get_allocator(); }

    // hash
    bool contains(param_type<T> t) const { return m_map.contains(ItemTrait<T>::key(t)); }
    auto key_at(usize idx) const { return m_order.at(idx); }

    auto query_idx(param_type<key_type> key) const -> std::optional<usize> {
        if (auto it = m_map.find(key); it != m_map.end()) return it->second;
        return std::nullopt;
    }
    T*       query(param_type<key_type> key) { return m_store->store_query(key); }
    T const* query(param_type<key_type> key) const { return m_store->store_query(key); }

    void set_store(QAbstractListModel* self, store_type store) {
        m_store = store;

        // TODO: no void*
        auto list       = QPointer { self };
        m_notify_handle = m_store->store_reg_notify([list, this](std::span<const key_type> keys) {
            if (! list) return;
            for (auto& key : keys) {
                if (auto it = m_map.find(key); it != m_map.end()) {
                    auto idx = list->index(it->second);
                    list->dataChanged(idx, idx);
                }
            }
        });
    }

protected:
    template<std::ranges::sized_range U>
    auto _insert_len(U&& range) {
        auto view = std::views::transform(range, [this](auto& el) -> usize {
            return m_map.contains(ItemTrait<T>::key(el)) ? 0 : 1;
        });
        return std::accumulate(view.begin(), view.end(), 0);
    }

    template<std::ranges::range U>
    void _insert_impl(usize it, U&& range) {
        std::vector<key_type, detail::rebind_alloc<allocator_type, key_type>> order(
            get_allocator());
        for (auto&& el : std::forward<U>(range)) {
            auto k = ItemTrait<T>::key(el);
            if (m_map.contains(k)) {
                m_store->store_insert(std::forward<decltype(el)>(el), false, m_notify_handle);
            } else {
                m_map.insert({ k, it + order.size() });
                order.emplace_back(k);
                m_store->store_insert(std::forward<decltype(el)>(el), true, m_notify_handle);
            }
        }
        m_order.insert(m_order.begin() + it, order.begin(), order.end());
    }

    void _erase_impl(usize index, usize last) {
        auto it    = m_order.begin();
        auto begin = it + index;
        auto end   = it + last;
        for (auto it = begin; it != end; it++) {
            m_map.erase(*it);
            m_store->store_remove(*it);
        }
        m_order.erase(it + index, it + last);
    }

    void _reset_impl() {
        for (auto& k : m_order) {
            m_store->store_remove(k);
        }
        m_map.clear();
        m_order.clear();
    }

    template<std::ranges::range U>
    void _reset_impl(U&& items) {
        for (auto& k : m_order) {
            m_store->store_remove(k);
        }
        m_map.clear();
        m_order.clear();
        _insert_impl(0, std::forward<U>(items));
    }

    void _move_impl(usize sourceRow, usize destinationRow, usize count) {
        auto it  = m_order.begin();
        auto src = it + sourceRow;
        auto dst = it + destinationRow;
        if (sourceRow > destinationRow) {
            std::rotate(dst, src, src + count);
            for (auto i = destinationRow; i < sourceRow + count; i++) {
                m_map.insert_or_assign(m_order.at(i), i);
            }
        } else {
            std::rotate(src, src + count, dst);
            for (auto i = sourceRow; i < destinationRow; i++) {
                m_map.insert_or_assign(m_order.at(i), i);
            }
        }
    }

private:
    struct Trans {
        ListImpl* self;

        T operator()(param_type<key_type> key) { return *(self->query(key)); }
    };

    std::vector<key_type, detail::rebind_alloc<allocator_type, key_type>> m_order;
    HashMap<key_type, usize, allocator_type>                              m_map;

    std::ranges::transform_view<std::ranges::ref_view<decltype(m_order)>, Trans> m_view;

    std::int64_t              m_notify_handle;
    std::optional<store_type> m_store;
};

} // namespace kstore::detail