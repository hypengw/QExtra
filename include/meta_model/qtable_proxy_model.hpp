#pragma once
#include <QtCore/QAbstractProxyModel>

namespace meta_model
{

class QTableProxyModel : public QAbstractProxyModel {
public:
    QTableProxyModel(QObject* parent = nullptr);
    auto mapFromSource(const QModelIndex& sourceIndex) const -> QModelIndex override;
    auto mapToSource(const QModelIndex& sourceIndex) const -> QModelIndex override;
};

} // namespace meta_model