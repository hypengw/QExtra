#include "kstore/qt/qunion_list_model.hpp"

#include <algorithm>

namespace kstore
{

// --- QUnionSource ---

QUnionSource::QUnionSource(QObject* parent): QObject(parent) {}

auto QUnionSource::model() const -> QAbstractItemModel* { return m_model; }

void QUnionSource::setModel(QAbstractItemModel* v) {
    if (m_model != v) {
        m_model = v;
        modelChanged();
    }
}

auto QUnionSource::mapping() const -> const QVariantMap& { return m_mapping; }

void QUnionSource::setMapping(const QVariantMap& v) {
    if (m_mapping != v) {
        m_mapping = v;
        mappingChanged();
    }
}

// --- QUnionListModel ---

QUnionListModel::QUnionListModel(QObject* parent): QAbstractListModel(parent) {
    m_offsets.push_back(0);
}

auto QUnionListModel::roles() const -> const QStringList& { return m_roles_list; }

void QUnionListModel::setRoles(const QStringList& v) {
    if (m_roles_list != v) {
        beginResetModel();
        m_roles_list = v;
        rebuildRoles();
        resolveAllMappings();
        endResetModel();
        rolesChanged();
    }
}

auto QUnionListModel::sources() const -> const QList<QUnionSource*>& { return m_sources; }

void QUnionListModel::setSources(const QList<QUnionSource*>& v) {
    beginResetModel();

    for (auto* src : m_sources) {
        disconnectSource(src);
    }

    m_sources = v;

    for (auto* src : m_sources) {
        connectSource(src);
        resolveMapping(src);
    }

    rebuildOffsets();
    endResetModel();
    sourcesChanged();
}

auto QUnionListModel::rowCount(const QModelIndex&) const -> int {
    return m_offsets.back();
}

auto QUnionListModel::data(const QModelIndex& index, int role) const -> QVariant {
    if (! index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};

    auto [src_idx, local_row] = locate(index.row());
    auto* src                 = m_sources[src_idx];

    if (! src->m_model) return {};

    auto it = src->m_resolved.constFind(role);
    if (it == src->m_resolved.constEnd()) return {};

    return src->m_model->data(src->m_model->index(local_row, 0), it.value());
}

auto QUnionListModel::roleNames() const -> QHash<int, QByteArray> { return m_roles; }

auto QUnionListModel::locate(int composite_row) const -> SourceLocation {
    // upper_bound finds first offset > composite_row
    auto it  = std::upper_bound(m_offsets.begin(), m_offsets.end(), composite_row);
    int  idx = static_cast<int>(std::distance(m_offsets.begin(), it)) - 1;
    return { idx, composite_row - m_offsets[idx] };
}

auto QUnionListModel::offsetOf(const QUnionSource* src) const -> int {
    for (int i = 0; i < m_sources.size(); i++) {
        if (m_sources[i] == src) return m_offsets[i];
    }
    return 0;
}

void QUnionListModel::connectSource(QUnionSource* src) {
    if (! src || ! src->m_model) return;

    auto* child = src->m_model;

    src->m_connections = {
        connect(child,
                &QAbstractItemModel::rowsAboutToBeInserted,
                this,
                [this, src](const QModelIndex&, int first, int last) {
                    auto off = offsetOf(src);
                    beginInsertRows({}, off + first, off + last);
                }),
        connect(child,
                &QAbstractItemModel::rowsInserted,
                this,
                [this]() {
                    rebuildOffsets();
                    endInsertRows();
                }),
        connect(child,
                &QAbstractItemModel::rowsAboutToBeRemoved,
                this,
                [this, src](const QModelIndex&, int first, int last) {
                    auto off = offsetOf(src);
                    beginRemoveRows({}, off + first, off + last);
                }),
        connect(child,
                &QAbstractItemModel::rowsRemoved,
                this,
                [this]() {
                    rebuildOffsets();
                    endRemoveRows();
                }),
        connect(child,
                &QAbstractItemModel::dataChanged,
                this,
                [this, src](const QModelIndex& tl, const QModelIndex& br, const QList<int>& roles) {
                    auto off = offsetOf(src);

                    // reverse-map child role ids to composite role ids
                    QList<int> composite_roles;
                    if (roles.isEmpty()) {
                        // all roles changed
                    } else {
                        for (auto child_role : roles) {
                            for (auto it = src->m_resolved.constBegin();
                                 it != src->m_resolved.constEnd();
                                 ++it) {
                                if (it.value() == child_role) {
                                    composite_roles.append(it.key());
                                }
                            }
                        }
                    }

                    auto top = index(off + tl.row(), 0);
                    auto bot = index(off + br.row(), 0);
                    dataChanged(top, bot, composite_roles);
                }),
        connect(child,
                &QAbstractItemModel::modelAboutToBeReset,
                this,
                [this]() {
                    beginResetModel();
                }),
        connect(child,
                &QAbstractItemModel::modelReset,
                this,
                [this]() {
                    rebuildOffsets();
                    endResetModel();
                }),
        connect(child, &QObject::destroyed, this, [this, src]() {
            auto off   = offsetOf(src);
            auto count = src->m_model ? src->m_model->rowCount() : 0;
            src->m_model = nullptr;
            src->m_resolved.clear();
            if (count > 0) {
                beginRemoveRows({}, off, off + count - 1);
                rebuildOffsets();
                endRemoveRows();
            }
        }),
    };

    // also react to source property changes
    connect(src, &QUnionSource::modelChanged, this, [this, src]() {
        beginResetModel();
        disconnectSource(src);
        connectSource(src);
        resolveMapping(src);
        rebuildOffsets();
        endResetModel();
    });

    connect(src, &QUnionSource::mappingChanged, this, [this, src]() {
        resolveMapping(src);
        auto off   = offsetOf(src);
        auto count = src->m_model ? src->m_model->rowCount() : 0;
        if (count > 0) {
            auto top = index(off, 0);
            auto bot = index(off + count - 1, 0);
            dataChanged(top, bot);
        }
    });
}

void QUnionListModel::disconnectSource(QUnionSource* src) {
    if (! src) return;
    for (auto& conn : src->m_connections) {
        disconnect(conn);
    }
    src->m_connections = {};
    src->m_resolved.clear();

    // disconnect source property signals from this
    disconnect(src, &QUnionSource::modelChanged, this, nullptr);
    disconnect(src, &QUnionSource::mappingChanged, this, nullptr);
}

void QUnionListModel::rebuildOffsets() {
    m_offsets.resize(m_sources.size() + 1);
    m_offsets[0] = 0;
    for (int i = 0; i < m_sources.size(); i++) {
        auto* m     = m_sources[i]->m_model;
        m_offsets[i + 1] = m_offsets[i] + (m ? m->rowCount() : 0);
    }
}

void QUnionListModel::rebuildRoles() {
    m_roles.clear();
    m_name_to_role.clear();
    for (int i = 0; i < m_roles_list.size(); i++) {
        int  role_id = Qt::UserRole + 1 + i;
        auto name    = m_roles_list[i].toUtf8();
        m_roles.insert(role_id, name);
        m_name_to_role.insert(name, role_id);
    }
}

void QUnionListModel::resolveMapping(QUnionSource* src) {
    src->m_resolved.clear();
    if (! src->m_model) return;

    auto child_roles = src->m_model->roleNames();

    // build reverse: child_role_name -> child_role_id
    QHash<QByteArray, int> child_name_to_role;
    for (auto it = child_roles.constBegin(); it != child_roles.constEnd(); ++it) {
        child_name_to_role.insert(it.value(), it.key());
    }

    // build mapping from QVariantMap: child_role_name -> composite_role_name
    QHash<QByteArray, QByteArray> explicit_map;
    for (auto it = src->m_mapping.constBegin(); it != src->m_mapping.constEnd(); ++it) {
        explicit_map.insert(it.key().toUtf8(), it.value().toString().toUtf8());
    }

    for (auto it = child_name_to_role.constBegin(); it != child_name_to_role.constEnd(); ++it) {
        const auto& child_name = it.key();
        int         child_role = it.value();

        QByteArray composite_name;
        if (auto eit = explicit_map.constFind(child_name); eit != explicit_map.constEnd()) {
            // explicit mapping: child_role_name -> composite_role_name
            composite_name = eit.value();
        } else if (m_name_to_role.contains(child_name)) {
            // auto-mapping: same name
            composite_name = child_name;
        } else {
            continue;
        }

        auto cit = m_name_to_role.constFind(composite_name);
        if (cit != m_name_to_role.constEnd()) {
            src->m_resolved.insert(cit.value(), child_role);
        }
    }
}

void QUnionListModel::resolveAllMappings() {
    for (auto* src : m_sources) {
        resolveMapping(src);
    }
}

} // namespace kstore
