#include "kstore/qt/meta_list_model.hpp"

#include <algorithm>

#include <QMetaProperty>

namespace kstore
{

QMetaListModel::QMetaListModel(QListInterface* oper, QObject* parent)
    : QAbstractListModel(parent),
      m_oper(oper),
      m_has_more(false),
      m_selection_enabled(false) {}

QMetaListModel::~QMetaListModel() {}
auto QMetaListModel::hasMore() const -> bool { return m_has_more; }
void QMetaListModel::setHasMore(bool v) {
    if (m_has_more != v) {
        m_has_more = v;
        hasMoreChanged(v);
    }
}

auto QMetaListModel::selectionEnabled() const -> bool { return m_selection_enabled; }

void QMetaListModel::setSelectionEnabled(bool v) {
    if (m_selection_enabled == v) return;

    layoutAboutToBeChanged();
    m_selection_enabled = v;
    if (! m_selection_enabled && ! m_selected_keys.isEmpty()) {
        m_selected_keys.clear();
        Q_EMIT selectedCountChanged();
        Q_EMIT selectionChanged();
    }
    layoutChanged();
    Q_EMIT selectionEnabledChanged();
    emitSelectionRolesChanged();
}

auto QMetaListModel::selectedCount() const -> qint32 {
    return static_cast<qint32>(m_selected_keys.size());
}

auto QMetaListModel::selectionKeyAt(qint32 row) const -> QString {
    if (row < 0 || row >= rowCount()) return {};
    return m_oper->rawKeyAt(row).toString();
}

auto QMetaListModel::isSelected(qint32 row) const -> bool {
    if (! m_selection_enabled) return false;
    const auto key = selectionKeyAt(row);
    return ! key.isEmpty() && m_selected_keys.contains(key);
}

auto QMetaListModel::setSelected(qint32 row, bool selected) -> bool {
    if (! m_selection_enabled) return false;
    const auto key = selectionKeyAt(row);
    if (key.isEmpty()) return false;

    const bool was_selected = m_selected_keys.contains(key);
    if (was_selected == selected) return false;

    if (selected)
        m_selected_keys.insert(key);
    else
        m_selected_keys.remove(key);

    const auto idx = index(row);
    Q_EMIT dataChanged(idx, idx, { SelectedRole });
    Q_EMIT selectedCountChanged();
    Q_EMIT selectionChanged();
    return true;
}

auto QMetaListModel::toggleSelected(qint32 row) -> bool {
    return setSelected(row, ! isSelected(row));
}

auto QMetaListModel::selectOnly(qint32 row) -> bool {
    if (! m_selection_enabled) return false;
    const auto key = selectionKeyAt(row);
    if (key.isEmpty()) return false;
    if (m_selected_keys.size() == 1 && m_selected_keys.contains(key)) return false;

    m_selected_keys.clear();
    m_selected_keys.insert(key);
    emitSelectionRolesChanged();
    Q_EMIT selectedCountChanged();
    Q_EMIT selectionChanged();
    return true;
}

auto QMetaListModel::selectRange(qint32 from, qint32 to, bool selected) -> bool {
    if (! m_selection_enabled) return false;
    const auto rows = rowCount();
    if (rows <= 0) return false;

    from = std::clamp(from, 0, rows - 1);
    to   = std::clamp(to, 0, rows - 1);
    if (from > to) std::swap(from, to);

    bool changed = false;
    for (auto row = from; row <= to; ++row) {
        const auto key = selectionKeyAt(row);
        if (key.isEmpty()) continue;
        const bool was_selected = m_selected_keys.contains(key);
        if (was_selected == selected) continue;
        changed = true;
        if (selected)
            m_selected_keys.insert(key);
        else
            m_selected_keys.remove(key);
    }

    if (! changed) return false;
    Q_EMIT dataChanged(index(from), index(to), { SelectedRole });
    Q_EMIT selectedCountChanged();
    Q_EMIT selectionChanged();
    return true;
}

void QMetaListModel::clearSelection() {
    if (m_selected_keys.isEmpty()) return;
    m_selected_keys.clear();
    emitSelectionRolesChanged();
    Q_EMIT selectedCountChanged();
    Q_EMIT selectionChanged();
}

void QMetaListModel::setSelectedKeys(const QStringList& keys) {
    QSet<QString> next;
    next.reserve(keys.size());
    for (const auto& key : keys) {
        if (! key.isEmpty()) next.insert(key);
    }
    if (next == m_selected_keys) return;
    m_selected_keys = std::move(next);
    emitSelectionRolesChanged();
    Q_EMIT selectedCountChanged();
    Q_EMIT selectionChanged();
}

auto QMetaListModel::selectedKeys() const -> QStringList {
    QStringList out;
    QSet<QString> seen;
    out.reserve(m_selected_keys.size());
    seen.reserve(m_selected_keys.size());

    for (auto row = 0; row < rowCount(); ++row) {
        const auto key = selectionKeyAt(row);
        if (! key.isEmpty() && m_selected_keys.contains(key)) {
            out.append(key);
            seen.insert(key);
        }
    }

    for (const auto& key : m_selected_keys) {
        if (! seen.contains(key)) out.append(key);
    }
    return out;
}

auto QMetaListModel::selectedRows() const -> QVariantList {
    QVariantList out;
    for (auto row = 0; row < rowCount(); ++row) {
        if (isSelected(row)) out.append(row);
    }
    return out;
}

auto QMetaListModel::selectedItems() const -> QVariantList {
    QVariantList out;
    for (auto row = 0; row < rowCount(); ++row) {
        if (isSelected(row)) out.append(item(row));
    }
    return out;
}

void QMetaListModel::emitSelectionRolesChanged() {
    const auto rows = rowCount();
    if (rows <= 0) return;
    Q_EMIT dataChanged(index(0), index(rows - 1), { SelectedRole });
}

bool QMetaListModel::canFetchMore(const QModelIndex&) const { return m_has_more; }
void QMetaListModel::fetchMore(const QModelIndex&) {
    setHasMore(false);
    reqFetchMore(rowCount());
}

int QMetaListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_oper->rawSize();
}

QVariant QMetaListModel::item(qint32 idx) const {
    if (idx < 0 || idx >= rowCount()) return {};
    return m_oper->rawToVariant(m_oper->rawAt(idx));
}
void QMetaListModel::setItem(qint32 idx, const QVariant& data) {
    if (idx < 0 || idx >= rowCount()) return;
    m_oper->rawAssign(idx, data);

    const auto index = this->index(idx);
    dataChanged(index, index);
}

auto QMetaListModel::items(qint32 offset, qint32 n) const -> QVariantList {
    if (n == -1) n = rowCount();

    QVariantList list;
    for (auto i = offset; i < n; i++) {
        list.emplace_back(item(i));
    }
    return list;
}

bool QMetaListModel::insertRows(int row, int count, const QModelIndex& parent) {
    const auto cur_count = rowCount(parent);
    if (row < 0) {
        row = cur_count + (1 + row);
    }
    if (count < 1 || row < 0 || row > cur_count) return false;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    m_oper->rawInsert(row, QVariantList(count));
    endInsertRows();
    return true;
}

auto QMetaListModel::removeRows(int row, int count, const QModelIndex& parent) -> bool {
    if (count <= 0 || row < 0 || (row + count) > rowCount(parent)) return false;

    beginRemoveRows(parent, row, row + count - 1);
    m_oper->rawErase(row, row + count);
    endRemoveRows();
    return true;
}

bool QMetaListModel::moveRows(const QModelIndex& sourceParent, int sourceRow, int count,
                              const QModelIndex& destinationParent, int destinationChild) {
#if 0
    if (sourceRow < 0 || sourceRow + count - 1 >= rowCount(sourceParent) || destinationChild < 0 ||
        destinationChild > rowCount(destinationParent) || sourceRow == destinationChild - 1 ||
        count <= 0 || sourceParent.isValid() || destinationParent.isValid()) {
        return false;
    }
#else
    if (sourceRow < 0 || sourceRow + count - 1 >= rowCount(sourceParent) || destinationChild < 0 ||
        destinationChild > rowCount(destinationParent) || sourceRow == destinationChild ||
        sourceRow == destinationChild - 1 || count <= 0 || sourceParent.isValid() ||
        destinationParent.isValid()) {
        return false;
    }
#endif

    if (! beginMoveRows(
            QModelIndex(), sourceRow, sourceRow + count - 1, QModelIndex(), destinationChild))
        return false;

    m_oper->rawMove(sourceRow, destinationChild, count);
    endMoveRows();
    return true;
}
Qt::DropActions QMetaListModel::supportedDropActions() const {
    return QAbstractItemModel::supportedDropActions() | Qt::MoveAction;
}

Qt::ItemFlags QMetaListModel::flags(const QModelIndex& index) const {
    if (! index.isValid()) return QAbstractListModel::flags(index) | Qt::ItemIsDropEnabled;
    return QAbstractListModel::flags(index) | Qt::ItemIsEditable | Qt::ItemIsDragEnabled |
           Qt::ItemIsDropEnabled;
}

auto QMetaListModel::roleNames() const -> QHash<int, QByteArray> {
    auto out = this->roleNamesRef();
    if (m_selection_enabled) out.insert(SelectedRole, "selected");
    return out;
}

} // namespace kstore
