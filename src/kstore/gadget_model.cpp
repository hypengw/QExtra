#include "kstore/qt/gadget_model.hpp"

auto kstore::readOnGadget(const QVariant& obj, const char* name) -> QVariant {
    if (auto meta = obj.metaType().metaObject()) {
        if (auto p = meta->property(meta->indexOfProperty(name)); p.isValid()) {
            return p.readOnGadget(obj.constData());
        }
    }
    return {};
}

namespace kstore
{

QGadgetListModel::QGadgetListModel(QListInterface* oper, QObject* parent)
    : QMetaListModel(oper, parent) {}
QVariant QGadgetListModel::data(const QModelIndex& index, int role) const {
    if (auto prop = this->propertyOfRole(role); prop) {
        return prop.value().readOnGadget(m_oper->rawAt(index.row()));
    }
    return {};
};
bool QGadgetListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    const auto row = index.row();
    if (row >= 0 && row < (qint32)m_oper->rawSize()) {
        if (auto prop = this->propertyOfRole(role); prop) {
            bool changed = prop.value().writeOnGadget(m_oper->rawAt(index.row()), value);
            if (changed) {
                dataChanged(index, index, { role });
            }
            return changed;
        }
    }
    return false;
}
} // namespace kstore