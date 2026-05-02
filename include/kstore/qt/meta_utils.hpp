#pragma once

#include <QtCore/QJsonDocument>
#include <QtCore/QMetaType>
#include <QtCore/QVariant>

namespace kstore
{

auto qvariant_to_josn(const QVariant&) -> QJsonValue;
auto qvariant_from_josn(const QMetaType&, const QJsonValue&) -> QVariant;

template<typename T>
auto qvariant_from_josn(const QJsonValue& val) -> std::optional<T> {
    auto out = qvariant_from_josn(QMetaType::fromType<T>(), val);
    if (out.isValid() && out.metaType() == QMetaType::fromType<T>()) {
        return out.template value<T>();
    }
    return std::nullopt;
}

} // namespace kstore