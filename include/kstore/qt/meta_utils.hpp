#pragma once

#include <QtCore/QJsonDocument>
#include <QtCore/QVariant>

namespace kstore
{

auto qvariant_to_josn(const QVariant&) -> QJsonDocument;
auto qvariant_from_josn(const QJsonDocument&) -> QVariant;

} // namespace kstore