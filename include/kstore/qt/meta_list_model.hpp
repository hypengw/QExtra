#pragma once

// workaround for QTBUG-83160
#if defined(Q_MOC_RUN)
#    define __cplusplus 202002
#endif

#include <QtCore/QAbstractItemModel>
#include "kstore/qt/meta_role.hpp"
#include "kstore/item_trait.hpp"
#include "kstore/list_impl.hpp"

namespace kstore
{

class QListInterface {
public:
    using value_t = void*;

    virtual auto rawAt(qint32 index) const -> value_t               = 0;
    virtual void rawAssign(qint32 index, const QVariant&)           = 0;
    virtual auto rawToVariant(value_t) const -> QVariant            = 0;
    virtual void rawInsert(qint32 index, std::span<const QVariant>) = 0;
    virtual void rawMove(qint32 src, qint32 dst, qint32 count = 1)  = 0;
    virtual auto rawItemMeta() const -> QMetaObject const*          = 0;
    virtual auto rawSize() const -> std::size_t                     = 0;
    virtual void rawErase(qint32 start, qint32 end)                 = 0;
};

template<typename TItem, typename IMPL, ListStoreType Store = ListStoreType::Vector,
         typename Allocator = std::allocator<detail::allocator_value_type<TItem, Store>>>
class QMetaListModelCRTP;

class QMetaListModel : public QAbstractListModel, public QMetaRoleNames {
    Q_OBJECT

    Q_PROPERTY(bool hasMore READ hasMore WRITE setHasMore NOTIFY hasMoreChanged)

    template<typename TItem, typename IMPL, ListStoreType Store, typename Allocator>
    friend class QMetaListModelCRTP;

public:
    QMetaListModel(QListInterface* oper, QObject* parent = nullptr);
    virtual ~QMetaListModel();

    Q_INVOKABLE QVariant     item(qint32 index) const;
    Q_INVOKABLE void         setItem(qint32 index, const QVariant&);
    Q_INVOKABLE QVariantList items(qint32 offset = 0, qint32 n = -1) const;
    // Q_INVOKABLE bool move(qint32 src, qint32 dst, qint32 count = 1);

    auto hasMore() const -> bool;
    void setHasMore(bool);

    bool canFetchMore(const QModelIndex&) const override;
    void fetchMore(const QModelIndex&) override;

    Q_SIGNAL void hasMoreChanged(bool);
    Q_SIGNAL void reqFetchMore(qint32);

    int rowCount(const QModelIndex& = QModelIndex()) const override;

    bool insertRows(int row, int count, const QModelIndex& parent = {}) override;

    bool removeRows(int row, int count, const QModelIndex& parent = {}) override;

    bool moveRows(const QModelIndex& sourceParent, int sourceRow, int count,
                  const QModelIndex& destinationParent, int destinationChild) override;
    auto roleNames() const -> QHash<int, QByteArray> override;

    Qt::DropActions supportedDropActions() const override;
    Qt::ItemFlags   flags(const QModelIndex& index) const override;

protected:
    QListInterface* m_oper;
    bool            m_has_more;
};

template<typename TItem, typename IMPL, ListStoreType Store, typename Allocator>
class QMetaListModelCRTP : public detail::ListImpl<TItem, Allocator, Store>, public QListInterface {
public:
    using list_impl_t    = detail::ListImpl<TItem, Allocator, Store>;
    using allocator_type = Allocator;
    template<typename T>
    using rebind_alloc = detail::rebind_alloc<allocator_type, T>;
    using value_type   = TItem;

    QMetaListModelCRTP(Allocator allc = Allocator()): list_impl_t(allc) {};
    QMetaListModelCRTP(const QMetaListModelCRTP&) = delete;

    virtual ~QMetaListModelCRTP() {};

    /// QlistInterface
    auto rawAt(qint32 index) const -> value_t override {
        // safe const_cast here
        // as item in container is not const
        return const_cast<TItem*>(&(_cimpl().at(index)));
    }
    void rawAssign(qint32 index, const QVariant& val) override {
        if (val.canConvert<TItem>()) {
            _cimpl().at(index) = val.value<TItem>();
        }
    }
    auto rawToVariant(value_t p) const -> QVariant override {
        if (p == nullptr) return {};
        auto& v = *(TItem*)p;
        if constexpr (special_of<value_type, std::variant>) {
            return std::visit(
                [](const auto& v) -> QVariant {
                    return QVariant::fromValue(v);
                },
                v);
        } else {
            return QVariant::fromValue(v);
        }
    }
    void rawInsert(qint32 offset, std::span<const QVariant> data) override {
        auto view = std::ranges::views::transform(data, [](const QVariant& v) {
            return v.value<TItem>();
        });
        _cimpl()._insert_impl(offset, view);
    }
    void rawMove(qint32 src, qint32 dst, qint32 count = 1) override {
        _cimpl()._move_impl(src, dst, count);
    }
    auto rawItemMeta() const -> QMetaObject const* override {
        if constexpr (requires { TItem::staticMetaObject; }) {
            return &TItem::staticMetaObject;
        } else {
            return nullptr;
        }
    }
    auto rawSize() const -> std::size_t override { return _cimpl().size(); }
    void rawErase(qint32 start, qint32 end) override { _cimpl()._erase_impl(start, end); }

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
        size = _cimpl()._insert_len(range);
        _cimpl().beginInsertRows({}, index, index + size - 1);
        _cimpl()._insert_impl(index, std::forward<T>(range));
        _cimpl().endInsertRows();
        return size;
    }
    void remove(int index, int size = 1) {
        if (size < 1) return;
        _cimpl().removeRows(index, size);
    }

    template<typename Func>
    void remove_if(Func&& func) {
        std::set<int, std::greater<>> indexes;
        const auto                    n = _cimpl().size();
        for (int i = 0; i < n; i++) {
            auto& el = _cimpl().at(i);
            if (func(el)) {
                indexes.insert(i);
            }
        }
        for (auto& i : indexes) {
            this->removeRow(i);
        }
    }
    void replace(int row, param_type<TItem> val) {
        auto& item = _cimpl().at(row);
        item       = val;
        auto idx   = _cimpl().index(row);
        _cimpl().dataChanged(idx, idx);
    }

    void resetModel() {
        _cimpl().beginResetModel();
        _cimpl()._reset_impl();
        _cimpl().endResetModel();
    }

    template<typename T>
        requires std::ranges::sized_range<T>
    void resetModel(const std::optional<T>& items) {
        _cimpl().beginResetModel();
        if (items) {
            _cimpl()._reset_impl(items.value());
        } else {
            _cimpl()._reset_impl();
        }
        _cimpl().endResetModel();
    }

    template<typename T>
        requires std::ranges::sized_range<T>
    // std::same_as<std::decay_t<typename T::value_type>, TItem>
    void resetModel(const T& items) {
        _cimpl().beginResetModel();
        _cimpl()._reset_impl(items);
        _cimpl().endResetModel();
    }
    template<typename T>
        requires std::ranges::sized_range<T>
    void replaceResetModel(const T& items) {
        const auto  size = items.size();
        const usize old  = std::max(_cimpl().size(), 0);
        const auto  num  = std::min<int>(old, size);
        for (auto i = 0; i < num; i++) {
            _cimpl().at(i) = items[i];
        }
        if (num > 0) _cimpl().dataChanged(_cimpl().index(0), _cimpl().index(num));
        if (size > old) {
            insert(num, std::ranges::subrange(items.begin() + num, items.end(), size - num));
        } else if (size < old) {
            removeRows(size, old - size);
        }
    }

    bool move(int sourceRow, int destinationRow, int count) {
        auto p = _cimpl().index(-1);
        return _cimpl().moveRows(p, sourceRow, count, p, destinationRow);
    }

    ///
    /// @brief sync items without reset
    /// if mostly changed, use reset
    template<detail::syncable_list<TItem> U>
    void sync(U&& items) {
        using key_type     = ItemTrait<TItem>::key_type;
        using idx_map_type = detail::HashMap<key_type, usize, allocator_type>;
        auto self          = &_cimpl();

        auto changed = [self](int row, int count = 1) {
            if (count < 1) return;
            auto first = self->index(row);
            auto last  = count > 1 ? self->index(row + count - 1) : first;
            self->dataChanged(first, last);
        };

        if constexpr (Store == ListStoreType::Vector) {
            // get key to idx map
            idx_map_type key_to_idx(self->get_allocator());
            key_to_idx.reserve(items.size());
            for (decltype(items.size()) i = 0; i < items.size(); ++i) {
                key_to_idx.insert({ ItemTrait<TItem>::key(items[i]), i });
            }

            // update existing, collect removals
            std::vector<usize> to_remove;
            for (usize i = 0; i < self->size(); i++) {
                auto key = ItemTrait<TItem>::key(self->at(i));
                if (auto it = key_to_idx.find(key); it != key_to_idx.end()) {
                    self->at(i) = std::forward<U>(items)[it->second];
                    key_to_idx.erase(it);
                } else {
                    to_remove.push_back(i);
                }
            }

            // batch remove back to front
            for (auto it = to_remove.rbegin(); it != to_remove.rend();) {
                usize last  = *it;
                usize first = last;
                ++it;
                while (it != to_remove.rend() && *it == first - 1) {
                    first = *it;
                    ++it;
                }
                self->remove(first, last - first + 1);
            }

            if (self->size() > 0) changed(0, self->size());
        } else if constexpr (Store == ListStoreType::Map || Store == ListStoreType::Share ||
                             Store == ListStoreType::VectorWithMap) {
            auto item_size = (usize)items.size();

            // build new key set
            idx_map_type new_key_to_idx(self->get_allocator());
            new_key_to_idx.reserve(item_size);
            for (usize i = 0; i < item_size; i++) {
                new_key_to_idx.insert({ ItemTrait<TItem>::key(items[i]), i });
            }

            // build old key set (before any modifications)
            idx_map_type old_key_to_idx(self->get_allocator());
            for (usize i = 0; i < (usize)self->size(); i++) {
                old_key_to_idx.insert({ self->key_at(i), i });
            }

            // ── Phase 1: batch remove items not in new list ──
            {
                std::vector<usize> to_remove;
                for (usize i = 0; i < (usize)self->size(); i++) {
                    if (! new_key_to_idx.contains(self->key_at(i))) {
                        to_remove.push_back(i);
                    }
                }
                for (auto it = to_remove.rbegin(); it != to_remove.rend();) {
                    usize last  = *it;
                    usize first = last;
                    ++it;
                    while (it != to_remove.rend() && *it == first - 1) {
                        first = *it;
                        ++it;
                    }
                    self->remove(first, last - first + 1);
                }
            }

            // ── Phase 2: reorder existing items via layoutChanged ──
            {
                using key_vec =
                    std::vector<key_type, rebind_alloc<key_type>>;
                key_vec target_order(self->get_allocator());
                target_order.reserve(self->size());
                for (usize i = 0; i < item_size; i++) {
                    auto key = ItemTrait<TItem>::key(items[i]);
                    if (old_key_to_idx.contains(key)) {
                        target_order.push_back(key);
                    }
                }

                bool needs_reorder = false;
                for (usize i = 0; i < target_order.size(); i++) {
                    if (target_order[i] != self->key_at(i)) {
                        needs_reorder = true;
                        break;
                    }
                }

                if (needs_reorder) {
                    // build key -> new position BEFORE reorder
                    idx_map_type key_to_new_pos(self->get_allocator());
                    for (usize i = 0; i < target_order.size(); i++) {
                        key_to_new_pos[target_order[i]] = i;
                    }

                    self->layoutAboutToBeChanged();
                    auto old_persistent = self->persistentIndexList();

                    // compute new persistent indexes (key_at still old order)
                    QModelIndexList new_persistent;
                    new_persistent.reserve(old_persistent.size());
                    for (auto& idx : old_persistent) {
                        if (idx.isValid() && idx.row() >= 0 &&
                            idx.row() < (int)self->size()) {
                            auto key = self->key_at(idx.row());
                            if (auto it = key_to_new_pos.find(key);
                                it != key_to_new_pos.end()) {
                                new_persistent.append(self->index(it->second));
                            } else {
                                new_persistent.append(QModelIndex());
                            }
                        } else {
                            new_persistent.append(QModelIndex());
                        }
                    }

                    self->_reorder_impl(target_order);
                    self->changePersistentIndexList(old_persistent, new_persistent);
                    self->layoutChanged();
                }
            }

            // ── Phase 3: update data for existing items ──
            {
                for (usize i = 0; i < (usize)self->size(); i++) {
                    auto key = self->key_at(i);
                    if (auto it = new_key_to_idx.find(key); it != new_key_to_idx.end()) {
                        self->at(i) = std::forward<U>(items)[it->second];
                    }
                }
                if (self->size() > 0) changed(0, self->size());
            }

            // ── Phase 4: insert new items ──
            {
                usize i   = 0;
                usize pos = 0;

                while (i < item_size) {
                    auto key = ItemTrait<TItem>::key(items[i]);
                    if (old_key_to_idx.contains(key)) {
                        pos++;
                        i++;
                    } else {
                        usize batch_start = i;
                        while (i < item_size &&
                               ! old_key_to_idx.contains(
                                   ItemTrait<TItem>::key(items[i]))) {
                            i++;
                        }
                        std::vector<TItem> batch;
                        batch.reserve(i - batch_start);
                        for (usize j = batch_start; j < i; j++) {
                            batch.push_back(std::forward<U>(items)[j]);
                        }
                        auto inserted = self->insert(pos, std::move(batch));
                        pos += inserted;
                    }
                }
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
        auto self          = &_cimpl();

        // get key to idx map
        idx_map_type key_to_idx(self->get_allocator());
        key_to_idx.reserve(items.size());
        for (decltype(items.size()) i = 0; i < items.size(); ++i) {
            key_to_idx.insert({ ItemTrait<TItem>::key(items[i]), i });
        }
        auto changed = [self](int row) {
            auto idx = self->index(row);
            self->dataChanged(idx, idx);
        };

        // update
        if constexpr (Store == ListStoreType::Vector) {
            for (usize i = 0; i < self->size(); ++i) {
                auto key = ItemTrait<TItem>::key(self->at(i));
                if (auto it = key_to_idx.find(key); it != key_to_idx.end()) {
                    self->at(i) = std::forward<U>(items)[it->second];
                    changed(i);
                    key_to_idx.erase(it);
                }
            }
        } else if constexpr (Store == ListStoreType::VectorWithMap) {
            for (auto& el : self->_maps()) {
                if (auto it = key_to_idx.find(el.first); it != key_to_idx.end()) {
                    self->at(el.second) = std::forward<U>(items)[it->second];
                    changed(el.second);
                    key_to_idx.erase(it);
                }
            }
        } else if constexpr (Store == ListStoreType::Map || Store == ListStoreType::Share) {
            for (usize i = 0; i < self->size(); ++i) {
                auto h = self->key_at(i);
                if (auto it = key_to_idx.find(h); it != key_to_idx.end()) {
                    self->at(i) = std::forward<U>(items)[it->second];
                    changed(i);
                    key_to_idx.erase(it);
                }
            }
        }

        // append new
        idx_set_type ids(self->get_allocator());
        for (auto& el : key_to_idx) {
            ids.insert(el.second);
        }
        for (auto id : ids) {
            self->insert(self->size(), std::forward<U>(items)[id]);
        }
        return ids.size();
    }

private:
    auto&       _cimpl() noexcept { return *static_cast<IMPL*>(this); }
    const auto& _cimpl() const noexcept { return *static_cast<const IMPL*>(this); }
};
} // namespace kstore
