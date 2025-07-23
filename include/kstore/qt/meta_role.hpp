#pragma once

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QMetaProperty>
#include <QtCore/QAbstractItemModel>

namespace kstore
{

struct QEmpty {
    Q_GADGET
};

class QMetaRoleNames {
public:
    QMetaRoleNames();

    auto meta() const -> const QMetaObject&;
    auto roleOf(QByteArrayView name) const -> int;

protected:
    void updateRoleNames(const QMetaObject& meta, QAbstractItemModel* model);
    auto roleNamesRef() const -> const QHash<int, QByteArray>&;
    auto nameRolesRef() const -> const QHash<QByteArray, int>&;
    auto propertyOfRole(int role) const -> std::optional<QMetaProperty>;

private:
    QHash<int, QByteArray> m_role_names;
    QHash<QByteArray, int> m_name_roles;
    QMetaObject            m_meta;
};

} // namespace kstore
