#pragma once

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QMetaProperty>
#include <QtCore/QMetaMethod>
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

    enum Option
    {
        WithMethod = 1,
    };
    auto options() const -> int;

    struct RoleInfo {
        QByteArray name;
        int        index;
        bool       is_method;
    };

protected:
    void updateRoleNames(const QMetaObject& meta, QAbstractItemModel* model, int opts);
    auto roleNamesRef() const -> const QHash<int, QByteArray>&;
    auto roleInfosRef() const -> const QHash<int, RoleInfo>&;
    auto nameRolesRef() const -> const QHash<QByteArray, int>&;
    auto propertyOfRole(int role) const -> std::optional<QMetaProperty>;
    auto methodOfRole(int role) const -> std::optional<QMetaMethod>;

private:
    QHash<int, RoleInfo>   m_role_infos;
    QHash<int, QByteArray> m_role_names;
    QHash<QByteArray, int> m_name_roles;
    QMetaObject            m_meta;
    int                    m_opts;
};

} // namespace kstore
