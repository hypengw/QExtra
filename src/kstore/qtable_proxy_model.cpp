#include "kstore/qt/qtable_proxy_model.hpp"

namespace kstore
{
QTableProxyModel::QTableProxyModel(QObject* parent): QAbstractProxyModel(parent) {
    connect(this, &QTableProxyModel::columnNamesChanged, this, &QTableProxyModel::syncColumns);
    connect(this, &QTableProxyModel::sourceModelChanged, this, &QTableProxyModel::syncColumns);
}

void QTableProxyModel::syncColumns() {
    if (! sourceModel()) return;
    beginResetModel();
    m_headers.clear();
    auto        roles = sourceModel()->roleNames();
    const auto& r     = roles.asKeyValueRange();
    for (auto i = 0; i < m_column_names.size(); i++) {
        auto& col = m_column_names[i];
        auto  it  = std::find_if(r.begin(), r.end(), [&col](auto v) {
            return v.second == col;
        });
        if (it != r.end()) {
            m_headers.push_back({ .role = it->first, .propname = it->second });
        }
    }
    endResetModel();
}

auto QTableProxyModel::columnNames() const -> const QStringList { return m_column_names; }
void QTableProxyModel::setColumnNames(const QStringList& v) {
    if (v != m_column_names) {
        m_column_names = v;
        columnNamesChanged();
    }
}

auto QTableProxyModel::mapFromSource(const QModelIndex& sourceIndex) const -> QModelIndex {
    return this->index(sourceIndex.row(), sourceIndex.column(), {});
}
auto QTableProxyModel::mapToSource(const QModelIndex& sourceIndex) const -> QModelIndex {
    return sourceModel()->index(sourceIndex.row(), 0);
}

void QTableProxyModel::setSourceModel(QAbstractItemModel* sourceModel) {
    QAbstractProxyModel::setSourceModel(sourceModel);

    if (sourceModel) {
        for (const QMetaObject::Connection& connection : std::as_const(m_source_connections))
            disconnect(connection);
    }
    m_source_connections = std::array<QMetaObject::Connection, 18> {
        connect(sourceModel,
                &QAbstractItemModel::dataChanged,
                this,
                &QTableProxyModel::sourceDataChanged),
        connect(sourceModel,
                &QAbstractItemModel::headerDataChanged,
                this,
                &QTableProxyModel::sourceHeaderDataChanged),
        connect(sourceModel,
                &QAbstractItemModel::rowsAboutToBeInserted,
                this,
                &QTableProxyModel::sourceRowsAboutToBeInserted),
        connect(sourceModel,
                &QAbstractItemModel::rowsInserted,
                this,
                &QTableProxyModel::sourceRowsInserted),
        connect(sourceModel,
                &QAbstractItemModel::columnsAboutToBeInserted,
                this,
                &QTableProxyModel::sourceColumnsAboutToBeInserted),
        connect(sourceModel,
                &QAbstractItemModel::columnsInserted,
                this,
                &QTableProxyModel::sourceColumnsInserted),
        connect(sourceModel,
                &QAbstractItemModel::rowsAboutToBeRemoved,
                this,
                &QTableProxyModel::sourceRowsAboutToBeRemoved),
        connect(sourceModel,
                &QAbstractItemModel::rowsRemoved,
                this,
                &QTableProxyModel::sourceRowsRemoved),
        connect(sourceModel,
                &QAbstractItemModel::columnsAboutToBeRemoved,
                this,
                &QTableProxyModel::sourceColumnsAboutToBeRemoved),
        connect(sourceModel,
                &QAbstractItemModel::columnsRemoved,
                this,
                &QTableProxyModel::sourceColumnsRemoved),
        connect(sourceModel,
                &QAbstractItemModel::rowsAboutToBeMoved,
                this,
                &QTableProxyModel::sourceRowsAboutToBeMoved),
        connect(
            sourceModel, &QAbstractItemModel::rowsMoved, this, &QTableProxyModel::sourceRowsMoved),
        connect(sourceModel,
                &QAbstractItemModel::columnsAboutToBeMoved,
                this,
                &QTableProxyModel::sourceColumnsAboutToBeMoved),
        connect(sourceModel,
                &QAbstractItemModel::columnsMoved,
                this,
                &QTableProxyModel::sourceColumnsMoved),
        connect(sourceModel,
                &QAbstractItemModel::layoutAboutToBeChanged,
                this,
                &QTableProxyModel::sourceLayoutAboutToBeChanged),
        connect(sourceModel,
                &QAbstractItemModel::layoutChanged,
                this,
                &QTableProxyModel::sourceLayoutChanged),
        connect(sourceModel,
                &QAbstractItemModel::modelAboutToBeReset,
                this,
                &QTableProxyModel::sourceAboutToBeReset),
        connect(sourceModel, &QAbstractItemModel::modelReset, this, &QTableProxyModel::sourceReset)
    };
}

auto QTableProxyModel::data(const QModelIndex& proxyIndex, int role) const -> QVariant {
    if (proxyIndex.isValid()) {
        auto column = proxyIndex.column();
        if ((std::size_t)column < m_headers.size()) {
            role = m_headers[column].role;
        }
    }
    return QAbstractProxyModel::data(proxyIndex, role);
}
auto QTableProxyModel::headerData(int section, Qt::Orientation orientation, int role) const
    -> QVariant {
    if (section > 0 && (std::size_t)section < m_headers.size()) {
        auto role = m_headers[section];
        return role.propname;
    }

    return QAbstractProxyModel::headerData(section, orientation, role);
}
auto QTableProxyModel::roleNames() const -> QHash<int, QByteArray> {
    return { { Qt::DisplayRole, "display" } };
}
auto QTableProxyModel::columnCount(const QModelIndex&) const -> int { return m_headers.size(); }
auto QTableProxyModel::rowCount(const QModelIndex&) const -> int {
    auto s   = sourceModel();
    auto row = 0;
    if (s) row = s->rowCount();
    return row;
}
auto QTableProxyModel::parent(const QModelIndex&) const -> QModelIndex { return QModelIndex {}; }
auto QTableProxyModel::index(int row, int column, const QModelIndex&) const -> QModelIndex {
    return QAbstractProxyModel::createIndex(row, column, nullptr);
}

void QTableProxyModel::sourceDataChanged() {}
void QTableProxyModel::sourceHeaderDataChanged() {}
void QTableProxyModel::sourceRowsAboutToBeInserted() {}
void QTableProxyModel::sourceRowsInserted() {}
void QTableProxyModel::sourceColumnsAboutToBeInserted() {}
void QTableProxyModel::sourceColumnsInserted() {}
void QTableProxyModel::sourceRowsAboutToBeRemoved() {}
void QTableProxyModel::sourceRowsRemoved() {}
void QTableProxyModel::sourceColumnsAboutToBeRemoved() {}
void QTableProxyModel::sourceColumnsRemoved() {}
void QTableProxyModel::sourceRowsAboutToBeMoved() {}
void QTableProxyModel::sourceRowsMoved() {}
void QTableProxyModel::sourceColumnsAboutToBeMoved() {}
void QTableProxyModel::sourceColumnsMoved() {}
void QTableProxyModel::sourceLayoutAboutToBeChanged(const QList<QPersistentModelIndex>&  sourceParents,
                                           QAbstractItemModel::LayoutChangeHint hint) {}
void QTableProxyModel::sourceLayoutChanged(const QList<QPersistentModelIndex>&  sourceParents,
                                           QAbstractItemModel::LayoutChangeHint hint) {

                                           }
void QTableProxyModel::sourceAboutToBeReset() { beginResetModel(); }
void QTableProxyModel::sourceReset() { endResetModel(); }

} // namespace kstore
