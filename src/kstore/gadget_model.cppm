export module qextra:kstore.gadget_model;
import :kstore.meta_list_model;
import qextra.qt;
import rstd.cppstd;

export namespace kstore
{

template<typename T>
concept cp_is_gadget = requires() { typename T::QtGadgetHelper; };
template<typename T>
concept cp_is_qobject = std::derived_from<T, QObject>;

auto readOnGadget(const QVariant&, const char*) -> QVariant;
class QGadgetListModel : public QMetaListModel {
public:
    template<typename T>
    QGadgetListModel(T* oper, QObject* parent = nullptr, int opts = 0)
        : QGadgetListModel(static_cast<QListInterface*>(oper), parent) {
        // must use static member func here, as virtual ptr is not inited here
        if (auto meta = oper->T::rawItemMeta()) {
            updateRoleNames(*meta, this, opts);
        }
    }
    explicit QGadgetListModel(QListInterface* oper, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
};

} // namespace kstore
