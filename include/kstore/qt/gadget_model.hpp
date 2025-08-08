#pragma once

#include <type_traits>

#include <QtCore/QMetaProperty>
#include <QtCore/QJsonObject>
#include "kstore/qt/meta_list_model.hpp"

namespace kstore
{

template<typename T>
concept cp_is_gadget = requires() { typename T::QtGadgetHelper; };
template<typename T>
concept cp_is_qobject = std::derived_from<T, QObject>;

auto readOnGadget(const QVariant&, const char*) -> QVariant;
class QGadgetListModel : public QMetaListModel {
public:
    template<typename T>
    QGadgetListModel(T* oper, QObject* parent = nullptr)
        : QGadgetListModel(static_cast<QListInterface*>(oper), parent) {
        if (auto meta = oper->T::rawItemMeta()) {
            updateRoleNames(*meta, this);
        }
    }
    explicit QGadgetListModel(QListInterface* oper, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
};

} // namespace kstore