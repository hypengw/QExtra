#pragma once

#include <QtCore/QJsonDocument>
#include <QtCore/QVariant>

namespace kstore
{

auto qvariant_to_josn(const QVariant&) -> QJsonValue;
auto qvariant_from_josn(const QJsonValue&) -> QVariant;

} // namespace kstore