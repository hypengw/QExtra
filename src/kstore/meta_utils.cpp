#include "kstore/qt/meta_utils.hpp"

namespace
{
}

auto kstore::qvariant_to_josn(const QVariant&) -> QJsonDocument { return {}; }
auto kstore::qvariant_from_josn(const QJsonDocument&) -> QVariant { return {}; }
