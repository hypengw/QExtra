#pragma once

#include <type_traits>

#include "kstore/qt/gadget_model.hpp"
#include "kstore/qt/meta_list_model.hpp"

namespace kstore
{

namespace detail
{
template<typename T>
    requires std::is_base_of_v<QObject, T>
class QObjectListModel : public QMetaListModel<T*, QObjectListModel<T>, ListStoreType::Vector> {
    template<typename, ListStoreType, typename, typename>
    friend class detail::QMetaListModelCRTP;

public:
    using base_type    = QMetaListModel<T*, QObjectListModel<T>, ListStoreType::Vector>;
    QObjectListModel() = delete;

    template<typename U = T>
        requires std::same_as<U, QObject>
    QObjectListModel(const QMetaObject& meta, bool is_owner, QObject* parent = nullptr)
        : base_type(parent), m_is_owner(is_owner) {
        this->updateRoleNames(meta);
    }
    template<typename U = T>
        requires(! std::same_as<U, QObject>)
    QObjectListModel(bool is_owner, QObject* parent = nullptr)
        : base_type(parent), m_is_owner(is_owner) {
        this->updateRoleNames(T::staticMetaObject);
    }
    virtual ~QObjectListModel() {}

    virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (auto prop = this->propertyOfRole(role); prop) {
            return prop.value().read(this->at(index.row()));
        }
        return {};
    };

private:
    template<std::ranges::sized_range U>
        requires std::convertible_to<typename std::ranges::range_value_t<U>, T*>
    void _insert_impl(std::size_t pos, U&& range) {
        if (m_is_owner) {
            for (auto&& r : range) {
                r->setParent(this);
            }
        }
        base_type::_insert_impl(pos, std::forward<U>(range));
    }

    bool m_is_owner;
};
} // namespace detail

using QObjectListModel = detail::QObjectListModel<QObject>;
} // namespace kstore