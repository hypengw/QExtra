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

/*
template<typename TGadget>
    requires cp_is_gadget<TGadget>
TGadget toGadget(const QJSValue& js) {
    TGadget out;
    if (! js.isNull()) {
        const auto&        var  = js.toVariant();
        const QMetaObject& meta = TGadget::staticMetaObject;
        if (var.metaType() == meta.metaType()) {
            out = var.value<TGadget>();
        } else {
            for (int i = 0; i < meta.propertyCount(); i++) {
                const auto& p = meta.property(i);
                if (js.hasProperty(p.name())) {
                    const auto& var = js.property(p.name()).toVariant();
                    p.writeOnGadget(&out, var);
                }
            }
        }
    }
    return out;
}
*/
auto readOnGadget(const QVariant&, const char*) -> QVariant;

class QGadgetListModel : public QMetaListModel {
public:
    QGadgetListModel(QListInterface* oper, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
};

// template<typename TGadget, ListStoreType Store = ListStoreType::Vector,
//          typename Allocator = std::allocator<detail::allocator_value_type<TGadget, Store>>>
//     requires cp_is_gadget<TGadget>
// class QGadgetListModel : public QMetaListModel<TGadget, QGadgetListModel<TGadget, Store,
// Allocator>,
//                                                Store, Allocator> {
} // namespace kstore