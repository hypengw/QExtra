#pragma once

// workaround for QTBUG-83160
#if defined(Q_MOC_RUN)
#    define __cplusplus 202002
#endif

#include <ranges>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <set>

#include <QtCore/QAbstractItemModel>
#include "meta_model/qmeta_model_base.hpp"
#include "meta_model/item_trait.hpp"
#include "meta_model/share_store.hpp"

namespace meta_model
{

enum class QMetaListStore
{
    Vector = 0,
    VectorWithMap,
    Map,
    Share
};

namespace detail
{

template<typename T, QMetaListStore>
struct allocator_helper {
    using value_type = T;
};
template<typename T>
struct allocator_helper<T, QMetaListStore::Map> {
    using value_type = std::pair<const usize, T>;
};

template<typename T, QMetaListStore S>
using allocator_value_type = allocator_helper<T, S>::value_type;

template<typename Allocator, typename T>
using rebind_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

template<typename T, typename Allocator>
using Set = std::set<T, std::less<>, rebind_alloc<Allocator, T>>;

template<typename K, typename V, typename Allocator>
using HashMap = std::unordered_map<K, V, std::hash<K>, std::equal_to<K>,
                                   rebind_alloc<Allocator, std::pair<const K, V>>>;

template<typename T, typename Allocator, QMetaListStore Store>
class ListImpl;

class QMetaListModelBase : public QMetaModelBase<QAbstractListModel> {
    Q_OBJECT

    Q_PROPERTY(bool hasMore READ hasMore WRITE setHasMore NOTIFY hasMoreChanged)
public:
    QMetaListModelBase(QObject* parent = nullptr);
    virtual ~QMetaListModelBase();

    Q_INVOKABLE virtual QVariant     item(qint32 index) const                       = 0;
    Q_INVOKABLE virtual QVariantList items(qint32 offset = 0, qint32 n = -1) const  = 0;
    Q_INVOKABLE virtual bool         move(qint32 src, qint32 dst, qint32 count = 1) = 0;

    auto hasMore() const -> bool;
    void setHasMore(bool);

    bool canFetchMore(const QModelIndex&) const override;
    void fetchMore(const QModelIndex&) override;

    Q_SIGNAL void hasMoreChanged(bool);
    Q_SIGNAL void reqFetchMore(qint32);

private:
    bool m_has_more;
};

template<typename TItem, QMetaListStore Store, typename Allocator, typename IMPL>
class QMetaListModelPre : public QMetaListModelBase {
public:
    using value_type = TItem;
    template<typename T>
    using rebind_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

    QMetaListModelPre(QObject* parent = nullptr): QMetaListModelBase(parent) {};
    virtual ~QMetaListModelPre() {};

    template<typename T>
        requires std::same_as<std::remove_cvref_t<T>, TItem>
    auto insert(int index, T&& item) {
        return insert(index, std::array { std::forward<T>(item) });
    }

    template<typename T>
        requires std::ranges::sized_range<T>
    auto insert(int index, T&& range) {
        auto size = range.size();
        if (size < 1) return size;
        size = crtp_impl()._insert_len(range);
        beginInsertRows({}, index, index + size - 1);
        crtp_impl()._insert_impl(index, std::forward<T>(range));
        endInsertRows();
        return size;
    }
    void remove(int index, int size = 1) {
        if (size < 1) return;
        removeRows(index, size);
    }
    auto removeRows(int row, int count, const QModelIndex& parent = {}) -> bool override {
        if (count < 1) return false;
        beginRemoveRows(parent, row, row + count - 1);
        crtp_impl()._erase_impl(row, row + count);
        endRemoveRows();
        return true;
    }
    template<typename Func>
    void remove_if(Func&& func) {
        std::set<int, std::greater<>> indexes;
        for (int i = 0; i < rowCount(); i++) {
            auto& el = crtp_impl().at(i);
            if (func(el)) {
                indexes.insert(i);
            }
        }
        for (auto& i : indexes) {
            removeRow(i);
        }
    }
    void replace(int row, param_type<TItem> val) {
        auto& item = crtp_impl().at(row);
        item       = val;
        auto idx   = index(row);
        dataChanged(idx, idx);
    }

    void resetModel() {
        beginResetModel();
        crtp_impl()._reset_impl();
        endResetModel();
    }

    template<typename T>
        requires std::ranges::sized_range<T>
    void resetModel(const std::optional<T>& items) {
        beginResetModel();
        if (items) {
            crtp_impl()._reset_impl(items.value());
        } else {
            crtp_impl()._reset_impl();
        }
        endResetModel();
    }

    template<typename T>
        requires std::ranges::sized_range<T>
    // std::same_as<std::decay_t<typename T::value_type>, TItem>
    void resetModel(const T& items) {
        beginResetModel();
        crtp_impl()._reset_impl(items);
        endResetModel();
    }
    template<typename T>
        requires std::ranges::sized_range<T>
    void replaceResetModel(const T& items) {
        auto  size = items.size();
        usize old  = std::max(rowCount(), 0);
        auto  num  = std::min<int>(old, size);
        for (auto i = 0; i < num; i++) {
            crtp_impl().at(i) = items[i];
        }
        if (num > 0) dataChanged(index(0), index(num));
        if (size > old) {
            insert(num, std::ranges::subrange(items.begin() + num, items.end(), size - num));
        } else if (size < old) {
            removeRows(size, old - size);
        }
    }

    bool moveRows(const QModelIndex& sourceParent, int sourceRow, int count,
                  const QModelIndex& destinationParent, int destinationChild) override {
        if (sourceRow < 0 || sourceRow + count - 1 >= rowCount(sourceParent) ||
            destinationChild < 0 || destinationChild > rowCount(destinationParent) ||
            sourceRow == destinationChild - 1 || count <= 0 || sourceParent.isValid() ||
            destinationParent.isValid()) {
            return false;
        }
        if (! beginMoveRows(
                QModelIndex(), sourceRow, sourceRow + count - 1, QModelIndex(), destinationChild))
            return false;

        crtp_impl()._move_impl(sourceRow, destinationChild - 1, count);
        endMoveRows();
        return true;
    }

    bool move(int sourceRow, int destinationRow, int count) override {
        auto p = index(-1);
        return moveRows(p, sourceRow, count, p, destinationRow + 1);
    }

    QVariant item(int idx) const override {
        if ((usize)std::max(idx, 0) >= crtp_impl().size()) return {};
        if constexpr (special_of<value_type, std::variant>) {
            return std::visit(
                [](const auto& v) -> QVariant {
                    return QVariant::fromValue(v);
                },
                crtp_impl().at(idx));
        } else {
            return QVariant::fromValue(crtp_impl().at(idx));
        }
    }

    auto items(qint32 offset = 0, qint32 n = -1) const -> QVariantList override {
        if (n == -1) n = rowCount();
        auto view = std::views::transform(std::views::iota(offset, n), [this](qint32 idx) {
            return item(idx);
        });
        return QVariantList { view.begin(), view.end() };
    }

    virtual int rowCount(const QModelIndex& = QModelIndex()) const override {
        return crtp_impl().size();
    }

private:
    auto&       crtp_impl() { return *static_cast<IMPL*>(this); }
    const auto& crtp_impl() const { return *static_cast<const IMPL*>(this); }
};
} // namespace detail

namespace detail
{
template<typename T, typename TItem>
concept syncable_list =
    std::ranges::sized_range<T> &&
    std::same_as<std::remove_cvref_t<std::ranges::range_value_t<T>>, TItem> && hashable_item<TItem>;

template<typename T, typename Allocator>
class ListImpl<T, Allocator, QMetaListStore::Vector> {
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
class ListImpl<T, Allocator, QMetaListStore::VectorWithMap> {
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
class ListImpl<T, Allocator, QMetaListStore::Map> {
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
class ListImpl<T, Allocator, QMetaListStore::Share> {
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

} // namespace detail

template<typename TItem, typename CRTP, QMetaListStore Store,
         typename Allocator = std::allocator<detail::allocator_value_type<TItem, Store>>>
class QMetaListModel : public detail::QMetaListModelPre<TItem, Store, Allocator, CRTP>,
                       public detail::ListImpl<TItem, Allocator, Store> {
    friend class detail::QMetaListModelPre<TItem, Store, Allocator, CRTP>;
    using base_type      = detail::QMetaListModelPre<TItem, Store, Allocator, CRTP>;
    using base_impl_type = detail::ListImpl<TItem, Allocator, Store>;

public:
    using allocator_type = Allocator;
    using container_type = base_impl_type::container_type;
    using iterator       = container_type::iterator;

    template<typename T>
    using rebind_alloc = detail::rebind_alloc<allocator_type, T>;

    QMetaListModel(QObject* parent = nullptr, Allocator allc = Allocator())
        : base_type(parent), base_impl_type(allc) {}
    virtual ~QMetaListModel() {}

    ///
    /// @brief sync items without reset
    /// if mostly changed, use reset
    template<detail::syncable_list<TItem> U>
    void sync(U&& items) {
        using key_type     = ItemTrait<TItem>::key_type;
        using idx_map_type = detail::HashMap<key_type, usize, allocator_type>;

        auto changed = [this](int row, int count = 1) {
            auto idx = this->index(row);
            if (count > 1) {
                auto end = this->index(row + count - 1);
                this->dataChanged(idx, end);
            } else {
                this->dataChanged(idx, idx);
            }
        };

        // update and remove
        if constexpr (Store == QMetaListStore::Vector) {
            // get key to idx map
            idx_map_type key_to_idx(this->get_allocator());
            key_to_idx.reserve(items.size());
            for (decltype(items.size()) i = 0; i < items.size(); ++i) {
                key_to_idx.insert({ ItemTrait<TItem>::key(items[i]), i });
            }
            for (usize i = 0; i < this->size();) {
                auto key = ItemTrait<TItem>::key(this->at(i));
                if (auto it = key_to_idx.find(key); it != key_to_idx.end()) {
                    this->at(i) = std::forward<U>(items)[it->second];
                    changed(it->second);
                    key_to_idx.erase(it);
                    ++i;
                } else {
                    this->remove(i);
                }
            }
        } else if constexpr (Store == QMetaListStore::Map || Store == QMetaListStore::Share ||
                             Store == QMetaListStore::VectorWithMap) {
            auto item_size = (usize)items.size();
            for (usize i = 0; i < item_size; i++) {
                auto key = ItemTrait<TItem>::key(items[i]);
                if (i < this->size()) {
                    auto cur_key = this->key_at(i);
                    if (key == cur_key) {
                        // do update
                        this->at(i) = std::forward<U>(items)[i];
                        changed(i);
                    } else {
                        if (auto idx = this->query_idx(key)) {
                            // try move more
                            usize j = 1;
                            for (; i + j < item_size && *idx + j < this->size(); j++) {
                                auto key = ItemTrait<TItem>::key(items[i + j]);
                                if (key != this->key_at(*idx + j)) {
                                    break;
                                }
                            }
                            // do move
                            auto ok = this->move(*idx, i, 1 + (j - 1));
                            Q_ASSERT(ok);
                            Q_ASSERT(key == this->key_at(i));
                            Q_ASSERT(cur_key == this->key_at(i + 1 + (j - 1)));

                            // do update
                            for (usize k = 0; k < j; k++) {
                                this->at(i + k) = std::forward<U>(items)[i + k];
                            }
                            changed(i, j);
                        } else {
                            // do remove and insert
                            this->remove(i);
                            this->insert(i, std::forward<U>(items)[i]);
                        }
                    }
                } else {
                    // do and insert
                    this->insert(i, std::forward<U>(items)[i]);
                }
            }
            if (item_size < this->size()) {
                // do remove
                this->remove(item_size, this->size() - item_size);
            }
        }
    }

    ///
    /// @brief sync items without reset
    /// if mostly changed, use reset
    /// @return increased size
    template<detail::syncable_list<TItem> U>
    auto extend(U&& items) -> usize {
        using key_type     = ItemTrait<TItem>::key_type;
        using idx_set_type = detail::Set<usize, allocator_type>;
        using idx_map_type = detail::HashMap<key_type, usize, allocator_type>;

        // get key to idx map
        idx_map_type key_to_idx(this->get_allocator());
        key_to_idx.reserve(items.size());
        for (decltype(items.size()) i = 0; i < items.size(); ++i) {
            key_to_idx.insert({ ItemTrait<TItem>::key(items[i]), i });
        }
        auto changed = [this](int row) {
            auto idx = this->index(row);
            this->dataChanged(idx, idx);
        };

        // update
        if constexpr (Store == QMetaListStore::Vector) {
            for (usize i = 0; i < this->size(); ++i) {
                auto key = ItemTrait<TItem>::key(this->at(i));
                if (auto it = key_to_idx.find(key); it != key_to_idx.end()) {
                    this->at(i) = std::forward<U>(items)[it->second];
                    changed(i);
                    key_to_idx.erase(it);
                }
            }
        } else if constexpr (Store == QMetaListStore::VectorWithMap) {
            for (auto& el : this->_maps()) {
                if (auto it = key_to_idx.find(el.first); it != key_to_idx.end()) {
                    this->at(el.second) = std::forward<U>(items)[it->second];
                    changed(el.second);
                    key_to_idx.erase(it);
                }
            }
        } else if constexpr (Store == QMetaListStore::Map || Store == QMetaListStore::Share) {
            for (usize i = 0; i < this->size(); ++i) {
                auto h = this->key_at(i);
                if (auto it = key_to_idx.find(h); it != key_to_idx.end()) {
                    this->at(i) = std::forward<U>(items)[it->second];
                    changed(i);
                    key_to_idx.erase(it);
                }
            }
        }

        // append new
        idx_set_type ids(this->get_allocator());
        for (auto& el : key_to_idx) {
            ids.insert(el.second);
        }
        for (auto id : ids) {
            this->insert(this->size(), std::forward<U>(items)[id]);
        }
        return ids.size();
    }
};
} // namespace meta_model