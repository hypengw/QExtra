#include "kstore/qt/gadget_model.hpp"
#include "kstore/qt/qtable_proxy_model.hpp"
#include "kstore/qt/qunion_list_model.hpp"
#include "tests.hpp"
#include <rstd/macro.hpp>

import qextra;

struct StoreValue {
  int id;
  int value;
  bool operator==(const StoreValue &) const = default;
};

template <> struct kstore::ItemTrait<StoreValue> {
  using key_type = int;
  using store_type = kstore::ShareStore<StoreValue>;
  static int key(const StoreValue &value) { return value.id; }
};

static void store_lifetime() {
  kstore::ShareStore<StoreValue> store;
  {
    auto [one, inserted] = store.store_insert({1, 10});
    rstd_assert(inserted);
    auto [same, changed] = store.store_insert({1, 10});
    rstd_assert(!changed);
    auto [two, second] = store.store_insert({2, 20});
    rstd_assert(second && store.size() == 2);
    one = one;
    one = std::move(one);
    same = two;
    one = std::move(two);
    rstd_assert(store.size() == 1 && store.store_query(1) == nullptr);
    rstd_assert(one->id == 2 && same->id == 2);
  }
  rstd_assert(store.size() == 0);
  int calls = 0;
  auto subscription = store.subscribe(1, [&](auto) { ++calls; });
  const std::array other{2};
  const std::array wanted{1};
  store.store_changed_callback(other);
  rstd_assert(calls == 0);
  store.store_changed_callback(wanted);
  rstd_assert(calls == 1);
  auto moved = std::move(subscription);
  moved.reset();
  moved.reset();
  store.store_changed_callback(wanted);
  rstd_assert(calls == 1);
  auto self_removing = store.subscribe(1, [&](auto) { moved.reset(); });
  moved = store.subscribe(1, [&](auto) { ++calls; });
  store.store_changed_callback(wanted);
  rstd_assert(calls == 1);
}

class SharedModel : public kstore::QMetaListModel,
    public kstore::QMetaListModelCRTP<StoreValue,SharedModel,kstore::ListStoreType::Share> {
    friend class kstore::QMetaListModelCRTP<StoreValue,SharedModel,kstore::ListStoreType::Share>;
public:
    SharedModel():kstore::QMetaListModel(this) {}
    QVariant data(const QModelIndex&,int) const override { return {}; }
};

static void shared_list_lifetime() {
    kstore::ShareStore<StoreValue> first, second;
    auto list=std::make_unique<SharedModel>();
    list->set_store(list.get(),first);
    list->insert(0,std::array{StoreValue{1,10}});
    rstd_assert(list->rowCount()==1&&first.size()==1);
    int removed=0,changes=0;
    QObject::connect(list.get(),&QAbstractItemModel::rowsRemoved,list.get(),[&]{++removed;});
    QObject::connect(list.get(),&QAbstractItemModel::dataChanged,list.get(),[&]{++changes;});
    list->set_store(list.get(),first);
    rstd_assert(first.size()==1&&removed==0);
    list->set_store(list.get(),second);
    rstd_assert(first.size()==0&&list->rowCount()==0&&removed==1);
    list->insert(0,std::array{StoreValue{1,20}});
    const auto before=changes;
    const std::array key{1};
    first.store_changed_callback(key);
    rstd_assert(changes==before);
    second.store_changed_callback(key);
    rstd_assert(changes==before+1);
    list.reset();
    rstd_assert(second.size()==0);
}

int run_kstore(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  store_lifetime();
  shared_list_lifetime();
  QueryList query;
  auto revision=query.beginRequest();
  rstd_assert(query.isCurrentRequest(revision));
  query.setSort(1);
  rstd_assert(!query.isCurrentRequest(revision));
  revision=query.beginRequest();
  query.delayReload();
  rstd_assert(!query.isCurrentRequest(revision));
  kstore::QMetaRoleNames roles;
  rstd_assert(roles.options() == 0);
  rstd_assert(roles.meta().className() == QStringLiteral("kstore::QEmpty"));

  QStringListModel source(QStringList{"one", "two"});
  kstore::QTableProxyModel table;
  table.setSourceModel(&source);
  table.setColumnNames(QStringList{"display"});
  rstd_assert(table.rowCount() == 2 && table.columnCount() == 1);
  rstd_assert(table.data(table.index(0, 0, {})).toString() == "one");
  rstd_assert(table.metaObject()->indexOfProperty("columnNames") >= 0);

  kstore::QUnionSource input;
  input.setModel(&source);
  kstore::QUnionListModel combined;
  combined.setRoles(QStringList{"display"});
  combined.setSources({&input});
  rstd_assert(combined.rowCount() == 2);
  rstd_assert(combined.metaObject()->indexOfProperty("sources") >= 0);

  const QVariant value(QStringLiteral("value"));
  auto json = kstore::qvariant_to_josn(value);
  rstd_assert(kstore::qvariant_from_josn(value.metaType(), json) == value);
  rstd_assert(!kstore::readOnGadget(QVariant(), "missing").isValid());
  return 0;
}
