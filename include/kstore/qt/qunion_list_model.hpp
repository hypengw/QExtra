#pragma once

#include <array>
#include <vector>

#include <QtCore/QAbstractListModel>
#include <QtCore/QVariantMap>

namespace kstore
{

/// QUnionListModel combines multiple child models into a single flat list
/// with role name remapping.
///
/// QML usage:
/// @code
/// QUnionListModel {
///     roles: ["title", "artist", "duration"]
///     sources: [
///         QUnionSource { model: albumModel; mapping: {"name": "title", "creator": "artist"} },
///         QUnionSource { model: songModel;  mapping: {"songTitle": "title", "singer": "artist"} }
///     ]
/// }
/// @endcode
///
/// - mapping direction: {child_role_name: composite_role_name}
/// - roles with the same name are auto-mapped without explicit mapping

class QUnionListModel;

class QUnionSource : public QObject {
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel* model READ model WRITE setModel NOTIFY modelChanged FINAL)
    Q_PROPERTY(QVariantMap mapping READ mapping WRITE setMapping NOTIFY mappingChanged FINAL)

public:
    explicit QUnionSource(QObject* parent = nullptr);

    auto model() const -> QAbstractItemModel*;
    void setModel(QAbstractItemModel*);

    auto mapping() const -> const QVariantMap&;
    void setMapping(const QVariantMap&);

    Q_SIGNAL void modelChanged();
    Q_SIGNAL void mappingChanged();

private:
    friend class QUnionListModel;

    QAbstractItemModel* m_model { nullptr };
    QVariantMap         m_mapping; // child_role_name -> composite_role_name

    // managed by QUnionListModel
    QHash<int, int>                        m_resolved; // composite_role_id -> child_role_id
    std::array<QMetaObject::Connection, 8> m_connections {};
};

class QUnionListModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QStringList roles READ roles WRITE setRoles NOTIFY rolesChanged FINAL)
    Q_PROPERTY(QList<QUnionSource*> sources READ sources WRITE setSources NOTIFY sourcesChanged FINAL)

public:
    explicit QUnionListModel(QObject* parent = nullptr);

    auto roles() const -> const QStringList&;
    void setRoles(const QStringList&);

    auto sources() const -> const QList<QUnionSource*>&;
    void setSources(const QList<QUnionSource*>&);

    Q_SIGNAL void rolesChanged();
    Q_SIGNAL void sourcesChanged();

    // QAbstractListModel
    auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;
    auto data(const QModelIndex& index, int role = Qt::DisplayRole) const -> QVariant override;
    auto roleNames() const -> QHash<int, QByteArray> override;

private:
    struct SourceLocation {
        int source_index;
        int local_row;
    };

    auto locate(int composite_row) const -> SourceLocation;
    auto offsetOf(const QUnionSource* src) const -> int;

    void connectSource(QUnionSource* src);
    void disconnectSource(QUnionSource* src);
    void rebuildOffsets();
    void rebuildRoles();
    void resolveMapping(QUnionSource* src);
    void resolveAllMappings();

    QStringList            m_roles_list;
    QHash<int, QByteArray> m_roles;        // role_id -> name
    QHash<QByteArray, int> m_name_to_role; // name -> role_id

    QList<QUnionSource*> m_sources;
    std::vector<int>     m_offsets; // size = m_sources.size()+1
};

} // namespace kstore
