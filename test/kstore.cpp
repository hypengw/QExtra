#include "kstore/qt/gadget_model.hpp"
#include "kstore/qt/qtable_proxy_model.hpp"
#include "kstore/qt/qunion_list_model.hpp"
#include "tests.hpp"
#include <rstd/macro.hpp>

import qextra;

int run_kstore(int argc, char **argv) {
  QCoreApplication app(argc, argv);
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
