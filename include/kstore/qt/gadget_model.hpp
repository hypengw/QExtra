#pragma once

#include <type_traits>

#include <QtCore/QMetaProperty>
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
    QGadgetListModel(QListInterface* oper, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
};
} // namespace kstore