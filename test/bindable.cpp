#include "QExtra/macro_qt.hpp"
#include "tests.hpp"
#include <QtCore/QObject>
#include <QtCore/QProperty>
#include <rstd/macro.hpp>

import qextra;

namespace {
struct Owner : QObject {
  int changes{}, last{};
  void changed(int value) {
    ++changes;
    last = value;
  }
  void changedWithoutValue() { ++changes; }
};
} // namespace

int run_bindable(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  Owner owner;
  ObjectBindableProperty<Owner, int, &Owner::changed> value(1, &owner);
  QBindable<int> bindable(&value);
  QProperty<int> source(2);
  QProperty<int> dependent([&] { return value.value() * 2; });
  int observations = 0;
  auto observer = value.onValueChanged([&] { ++observations; });
  rstd_assert(value.binding().isNull() && !bindable.hasBinding());
  rstd_assert(dependent.value() == 2 && owner.changes == 0);

  value = 3;
  rstd_assert(owner.changes == 1 && owner.last == 3 && observations == 1);
  rstd_assert(dependent.value() == 6);
  value = 3;
  rstd_assert(owner.changes == 1 && observations == 1);

  bindable.setBinding([&] { return source.value() + 1; });
  rstd_assert(!bindable.binding().isNull() && value.hasBinding());
  rstd_assert(owner.changes == 1);
  source = 4;
  rstd_assert(value.value() == 5 && dependent.value() == 10);
  rstd_assert(owner.changes == 2 && owner.last == 5 && observations == 2);

  auto saved = bindable.binding();
  rstd_assert(value.hasBinding() && owner.changes == 2);
  auto taken = value.takeBinding();
  rstd_assert(!taken.isNull() && !value.hasBinding());
  source = 8;
  rstd_assert(value.value() == 5 && owner.changes == 2);
  value.setBinding(saved);
  rstd_assert(value.value() == 9 && owner.changes == 3);
  value.setValue(9);
  rstd_assert(!value.hasBinding() && owner.changes == 3);
  source = 10;
  rstd_assert(value.value() == 9);

  const QUntypedPropertyBinding wrong =
      Qt::makePropertyBinding([] { return QString(); });
  rstd_assert(!value.setBinding(wrong));
  value.setValueBypassingBindings(12);
  rstd_assert(owner.changes == 3);
  value.notify();
  rstd_assert(owner.changes == 4 && observations == 4 &&
              dependent.value() == 24);

  ObjectBindableProperty<Owner, int, &Owner::changedWithoutValue> noArgument(
      &owner);
  noArgument = 1;
  rstd_assert(owner.changes == 5);
  ObjectBindableProperty<Owner, int> noSignal(&owner);
  noSignal = 1;
  rstd_assert(noSignal.value() == 1 && owner.changes == 5);

  {
    ObjectBindableProperty<Owner, int, &Owner::changed> temporary(&owner);
    temporary.setBinding([&] { return source.value(); });
  }
  const auto before = owner.changes;
  source = 20;
  rstd_assert(owner.changes == before);

  Owner groupedOwner;
  ObjectBindableProperty<Owner, int, &Owner::changed> grouped(&groupedOwner);
  grouped.setBinding([&] { return source.value(); });
  const auto groupedBefore = groupedOwner.changes;
  Qt::beginPropertyUpdateGroup();
  source = 21;
  source = 22;
  Qt::endPropertyUpdateGroup();
  rstd_assert(grouped.value() == 22 && groupedOwner.last == 22);
  rstd_assert(groupedOwner.changes == groupedBefore + 1);

  QAsyncResult result;
  int statuses = 0, errors = 0, queries = 0;
  QObject::connect(&result, &QAsyncResult::statusChanged, &result,
                   [&](QAsyncResult::Status) { ++statuses; });
  QObject::connect(&result, &QAsyncResult::errorChanged, &result,
                   [&](const QString &) { ++errors; });
  QObject::connect(&result, &QAsyncResult::queryingChanged, &result,
                   [&](bool) { ++queries; });
  result.setStatus(QAsyncResult::Status::Querying);
  rstd_assert(result.querying() && statuses == 1 && queries == 1);
  result.setStatus(QAsyncResult::Status::Uninitialized);
  rstd_assert(!result.querying() && statuses == 2 && queries == 2);
  statuses = queries = 0;
  QProperty<QAsyncResult::Status> status(QAsyncResult::Status::Finished);
  QProperty<QString> error(QString::fromUtf8("first"));
  QProperty<bool> querying(true);
  result.bindableStatus().setBinding([&] { return status.value(); });
  result.bindableError().setBinding([&] { return error.value(); });
  result.bindableQuerying().setBinding([&] { return querying.value(); });
  rstd_assert(!result.bindableStatus().binding().isNull());
  rstd_assert(!result.bindableError().binding().isNull());
  rstd_assert(!result.bindableQuerying().binding().isNull());
  rstd_assert(statuses == 1 && errors == 1 && queries == 1);
  status = QAsyncResult::Status::Error;
  error = QString::fromUtf8("second");
  querying = false;
  rstd_assert(result.status() == status.value() &&
              result.error() == error.value());
  rstd_assert(!result.querying() && statuses == 2 && errors == 2 &&
              queries == 2);
  result.setStatus(result.status());
  result.setError(result.error());
  rstd_assert(!result.bindableStatus().hasBinding() &&
              !result.bindableError().hasBinding());
  rstd_assert(statuses == 2 && errors == 2);
  return 0;
}
