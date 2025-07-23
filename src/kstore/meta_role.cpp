#include "kstore/qt/meta_role.hpp"

namespace kstore
{

namespace detail
{
void update_role_names(QHash<int, QByteArray>& role_names, const QMetaObject& meta) {
    role_names.clear();

    auto roleIndex = Qt::UserRole + 1;
    for (auto i = 0; i < meta.propertyCount(); i++) {
        auto prop = meta.property(i);
        role_names.insert(roleIndex, prop.name());
        ++roleIndex;
    }
}
} // namespace detail

QMetaRoleNames::QMetaRoleNames(): m_meta(QEmpty::staticMetaObject) {}

auto QMetaRoleNames::meta() const -> const QMetaObject& { return m_meta; }
auto QMetaRoleNames::roleOf(QByteArrayView name) const -> int {
    auto& map = nameRolesRef();
    if (auto it = map.find(name); it != map.end()) {
        auto out = it.value();
        return out;
    }
    return -1;
}
void QMetaRoleNames::updateRoleNames(const QMetaObject& meta, QAbstractItemModel* model) {
    if (model) model->layoutAboutToBeChanged();

    m_meta = meta;
    {
        m_role_names.clear();
        m_name_roles.clear();

        auto roleIndex = Qt::UserRole + 1;
        for (auto i = 0; i < meta.propertyCount(); i++) {
            auto       prop = meta.property(i);
            const auto name = prop.name();
            m_role_names.insert(roleIndex, name);
            m_name_roles.insert(name, roleIndex);
            ++roleIndex;
        }
    }

    if (model) model->layoutChanged();
}
auto QMetaRoleNames::roleNamesRef() const -> const QHash<int, QByteArray>& { return m_role_names; }
auto QMetaRoleNames::nameRolesRef() const -> const QHash<QByteArray, int>& { return m_name_roles; }
auto QMetaRoleNames::propertyOfRole(int role) const -> std::optional<QMetaProperty> {
    if (auto prop_idx = meta().indexOfProperty(roleNamesRef().value(role).constData());
        prop_idx != -1) {
        return meta().property(prop_idx);
    }
    return std::nullopt;
}

} // namespace kstore