#pragma once
#include <QtCore/QAbstractProxyModel>

namespace meta_model
{

class QTableProxyModel : public QAbstractProxyModel {
    Q_OBJECT

    Q_PROPERTY(QStringList columnNames READ columnNames WRITE setColumnNames NOTIFY
                   columnNamesChanged FINAL)
public:
    QTableProxyModel(QObject* parent = nullptr);

    auto          columnNames() const -> const QStringList;
    void          setColumnNames(const QStringList&);
    Q_SIGNAL void columnNamesChanged();

    auto mapFromSource(const QModelIndex& sourceIndex) const -> QModelIndex override;
    auto mapToSource(const QModelIndex& sourceIndex) const -> QModelIndex override;

    void setSourceModel(QAbstractItemModel* sourceModel) override;

    auto data(const QModelIndex& proxyIndex, int role = Qt::DisplayRole) const -> QVariant override;
    auto headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const
        -> QVariant override;
    auto roleNames() const -> QHash<int, QByteArray> override;
    auto columnCount(const QModelIndex& parent = QModelIndex()) const -> int override;
    auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;

    auto parent(const QModelIndex& child) const -> QModelIndex override;
    auto index(int row, int column, const QModelIndex& parent) const -> QModelIndex override;

private:
    Q_SLOT void syncColumns();

    Q_SLOT void sourceDataChanged();
    Q_SLOT void sourceHeaderDataChanged();
    Q_SLOT void sourceRowsAboutToBeInserted();
    Q_SLOT void sourceRowsInserted();
    Q_SLOT void sourceColumnsAboutToBeInserted();
    Q_SLOT void sourceColumnsInserted();
    Q_SLOT void sourceRowsAboutToBeRemoved();
    Q_SLOT void sourceRowsRemoved();
    Q_SLOT void sourceColumnsAboutToBeRemoved();
    Q_SLOT void sourceColumnsRemoved();
    Q_SLOT void sourceRowsAboutToBeMoved();
    Q_SLOT void sourceRowsMoved();
    Q_SLOT void sourceColumnsAboutToBeMoved();
    Q_SLOT void sourceColumnsMoved();
    Q_SLOT void sourceLayoutAboutToBeChanged(const QList<QPersistentModelIndex>&  sourceParents,
                                             QAbstractItemModel::LayoutChangeHint hint);
    Q_SLOT void sourceLayoutChanged(const QList<QPersistentModelIndex>&  sourceParents,
                                    QAbstractItemModel::LayoutChangeHint hint);
    Q_SLOT void sourceAboutToBeReset();
    Q_SLOT void sourceReset();
    struct HeaderData {
        int     role;
        QString propname;
    };
    std::vector<HeaderData> m_headers;
    QHash<int, QByteArray>  m_rolenames;
    QStringList             m_column_names;

    std::array<QMetaObject::Connection, 18> m_source_connections;
};

} // namespace meta_model