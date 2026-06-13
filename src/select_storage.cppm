module;
#include "QExtra/macro_qt.hpp"
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>

#ifdef Q_MOC_RUN
#    include "QExtra/select_storage.moc"
#endif

export module qextra:select_storage;
export import qt;
export import :kstore;

export class SelectStorage : public QObject {
    Q_OBJECT

    Q_PROPERTY(QObject* model READ model WRITE setModel NOTIFY modelChanged FINAL)
    Q_PROPERTY(bool selectionMode READ selectionMode WRITE setSelectionMode NOTIFY
                   selectionModeChanged FINAL)
    Q_PROPERTY(qint32 anchorIndex READ anchorIndex WRITE setAnchorIndex NOTIFY anchorIndexChanged
                   FINAL)
    Q_PROPERTY(qint32 selectedCount READ selectedCount NOTIFY selectedCountChanged FINAL)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)

public:
    SelectStorage(QObject* parent = nullptr);
    ~SelectStorage() override;

    auto model() const -> QObject*;
    void setModel(QObject* model);

    auto selectionMode() const -> bool;
    void setSelectionMode(bool v);

    auto anchorIndex() const -> qint32;
    void setAnchorIndex(qint32 v);

    auto selectedCount() const -> qint32;
    auto active() const -> bool;

    Q_INVOKABLE bool begin(qint32 row = -1);
    Q_INVOKABLE bool setSelected(qint32 row, bool selected);
    Q_INVOKABLE bool toggleSelected(qint32 row);
    Q_INVOKABLE bool selectRange(qint32 from, qint32 to, bool selected = true);
    Q_INVOKABLE virtual void clear();
    Q_INVOKABLE void setSelectedKeys(const QStringList& keys);
    Q_INVOKABLE QStringList selectedKeys() const;
    Q_INVOKABLE QVariantList selectedItems() const;

    Q_SIGNAL void modelChanged();
    Q_SIGNAL void selectionModeChanged();
    Q_SIGNAL void anchorIndexChanged();
    Q_SIGNAL void selectedCountChanged();
    Q_SIGNAL void activeChanged();

protected:
    virtual auto keepActiveWithoutSelection() const -> bool;
    auto         selectionModel() const -> kstore::QMetaListModel*;
    void         notifyActiveChanged();

private:
    void cancelIfEmpty();
    void connectModel();
    void disconnectModel();

    QPointer<kstore::QMetaListModel> m_model;
    bool                             m_selection_mode;
    qint32                           m_anchor_index;
    bool                             m_active;
};
