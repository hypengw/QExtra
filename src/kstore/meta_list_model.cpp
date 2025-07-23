#include "kstore/qt/meta_list_model.hpp"

#include <QMetaProperty>

namespace kstore
{

QMetaListModel::QMetaListModel(QListInterface* oper, QObject* parent)
    : QAbstractListModel(parent), m_oper(oper), m_has_more(false) {
    if (auto meta = oper->rawItemMeta()) {
        updateRoleNames(*meta, this);
    }
}
QMetaListModel::~QMetaListModel() {}
auto QMetaListModel::hasMore() const -> bool { return m_has_more; }
void QMetaListModel::setHasMore(bool v) {
    if (m_has_more != v) {
        m_has_more = v;
        hasMoreChanged(v);
    }
}
bool QMetaListModel::canFetchMore(const QModelIndex&) const { return m_has_more; }
void QMetaListModel::fetchMore(const QModelIndex&) {
    setHasMore(false);
    reqFetchMore(rowCount());
}

int QMetaListModel::rowCount(const QModelIndex&) const { return m_oper->rawSize(); }

QVariant QMetaListModel::item(int idx) const {
    if (std::max(idx, 0) >= rowCount()) return {};
    return m_oper->rawToVariant(m_oper->rawAt(idx));
}
auto QMetaListModel::items(qint32 offset, qint32 n) const -> QVariantList {
    if (n == -1) n = rowCount();

    QVariantList list;
    for (auto i = offset; i < n; i++) {
        list.emplace_back(item(i));
    }
    return list;
}

auto QMetaListModel::removeRows(int row, int count, const QModelIndex& parent) -> bool {
    if (count < 1) return false;
    beginRemoveRows(parent, row, row + count - 1);
    m_oper->rawErase(row, row + count);
    endRemoveRows();
    return true;
}

bool QMetaListModel::moveRows(const QModelIndex& sourceParent, int sourceRow, int count,
                              const QModelIndex& destinationParent, int destinationChild) {
    if (sourceRow < 0 || sourceRow + count - 1 >= rowCount(sourceParent) || destinationChild < 0 ||
        destinationChild > rowCount(destinationParent) || sourceRow == destinationChild - 1 ||
        count <= 0 || sourceParent.isValid() || destinationParent.isValid()) {
        return false;
    }
    if (! beginMoveRows(
            QModelIndex(), sourceRow, sourceRow + count - 1, QModelIndex(), destinationChild))
        return false;

    m_oper->rawMove(sourceRow, destinationChild, count);
    endMoveRows();
    return true;
}

auto QMetaListModel::roleNames() const -> QHash<int, QByteArray> { return this->roleNamesRef(); }

} // namespace kstore