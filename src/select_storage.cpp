module;
#include "QExtra/select_storage.moc.h"

module qextra;
import :select_storage;

SelectStorage::SelectStorage(QObject* parent)
    : QObject(parent), m_selection_mode(false), m_anchor_index(-1), m_active(false) {}

SelectStorage::~SelectStorage() { disconnectModel(); }

auto SelectStorage::model() const -> QObject* { return m_model; }

void SelectStorage::setModel(QObject* model) {
    auto* next = qobject_cast<kstore::QMetaListModel*>(model);
    if (m_model == next) return;

    disconnectModel();
    m_model = next;
    connectModel();

    Q_EMIT modelChanged();
    Q_EMIT selectedCountChanged();
    cancelIfEmpty();
    notifyActiveChanged();
}

auto SelectStorage::selectionMode() const -> bool { return m_selection_mode; }

void SelectStorage::setSelectionMode(bool v) {
    if (m_selection_mode == v) return;
    m_selection_mode = v;
    Q_EMIT selectionModeChanged();
    notifyActiveChanged();
}

auto SelectStorage::anchorIndex() const -> qint32 { return m_anchor_index; }

void SelectStorage::setAnchorIndex(qint32 v) {
    if (m_anchor_index == v) return;
    m_anchor_index = v;
    Q_EMIT anchorIndexChanged();
}

auto SelectStorage::selectedCount() const -> qint32 {
    return m_model ? m_model->selectedCount() : 0;
}

auto SelectStorage::active() const -> bool {
    return m_selection_mode || selectedCount() > 0 || keepActiveWithoutSelection();
}

auto SelectStorage::begin(qint32 row) -> bool {
    auto* model = selectionModel();
    if (! model) {
        setSelectionMode(false);
        setAnchorIndex(-1);
        return false;
    }

    if (row >= 0) {
        model->setSelected(row, true);
        setAnchorIndex(row);
    }

    if (selectedCount() == 0 && ! keepActiveWithoutSelection()) {
        setSelectionMode(false);
        setAnchorIndex(-1);
        return false;
    }

    setSelectionMode(true);
    return true;
}

auto SelectStorage::setSelected(qint32 row, bool selected) -> bool {
    auto* model = selectionModel();
    if (! model) return false;
    auto changed = model->setSelected(row, selected);
    cancelIfEmpty();
    return changed;
}

auto SelectStorage::toggleSelected(qint32 row) -> bool {
    auto* model = selectionModel();
    if (! model) return false;

    const auto changed = model->toggleSelected(row);
    const auto hasSelection = selectedCount() > 0;
    setSelectionMode(hasSelection || keepActiveWithoutSelection());
    setAnchorIndex(hasSelection ? row : -1);
    return changed;
}

auto SelectStorage::selectRange(qint32 from, qint32 to, bool selected) -> bool {
    auto* model = selectionModel();
    if (! model) return false;

    const auto changed = model->selectRange(from, to, selected);
    if (selected && selectedCount() > 0)
        setSelectionMode(true);
    else
        cancelIfEmpty();
    return changed;
}

void SelectStorage::clear() {
    if (m_model) m_model->clearSelection();
    setSelectionMode(false);
    setAnchorIndex(-1);
    notifyActiveChanged();
}

void SelectStorage::setSelectedKeys(const QStringList& keys) {
    auto* model = selectionModel();
    if (! model) return;
    model->setSelectedKeys(keys);
    cancelIfEmpty();
}

auto SelectStorage::selectedKeys() const -> QStringList {
    return m_model ? m_model->selectedKeys() : QStringList {};
}

auto SelectStorage::selectedItems() const -> QVariantList {
    return m_model ? m_model->selectedItems() : QVariantList {};
}

auto SelectStorage::keepActiveWithoutSelection() const -> bool { return false; }

auto SelectStorage::selectionModel() const -> kstore::QMetaListModel* { return m_model; }

void SelectStorage::notifyActiveChanged() {
    const auto next = active();
    if (m_active == next) return;
    m_active = next;
    Q_EMIT activeChanged();
}

void SelectStorage::cancelIfEmpty() {
    if (selectedCount() > 0 || keepActiveWithoutSelection()) {
        notifyActiveChanged();
        return;
    }
    setSelectionMode(false);
    setAnchorIndex(-1);
    notifyActiveChanged();
}

void SelectStorage::connectModel() {
    if (! m_model) return;
    connect(m_model, &kstore::QMetaListModel::selectedCountChanged, this, [this] {
        Q_EMIT selectedCountChanged();
        cancelIfEmpty();
        notifyActiveChanged();
    });
}

void SelectStorage::disconnectModel() {
    if (! m_model) return;
    disconnect(m_model, nullptr, this, nullptr);
}

#include "QExtra/select_storage.moc.cpp"
