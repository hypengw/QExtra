#include "kstore/qt/meta_utils.hpp"
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QMetaObject>
#include <QtCore/QMetaProperty>
#include <QtCore/QSequentialIterable>
#include <QtCore/QAssociativeIterable>

namespace
{
auto qgadget_to_json(const QVariant& variant) -> QJsonObject {
    auto obj  = QJsonObject();
    auto meta = variant.metaType().metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        auto prop = meta->property(i);
        if (prop.metaType().flags() & QMetaType::IsPointer) {
            // ignore
        } else if (prop.isReadable() && prop.isStored()) {
            auto value = prop.readOnGadget(variant.constData());
            obj.insert(prop.name(), kstore::qvariant_to_josn(value));
        }
    }
    return obj;
}
} // namespace

auto kstore::qvariant_to_josn(const QVariant& variant) -> QJsonValue {
    if (auto p = get_if<QVariantMap>(&variant)) {
        auto obj = QJsonObject();
        for (auto it = p->constBegin(); it != p->constEnd(); ++it) {
            obj.insert(it.key(), qvariant_to_josn(it.value()));
        }
        return obj;
    } else if (auto p = get_if<QVariantHash>(&variant)) {
        auto obj = QJsonObject();
        for (auto it = p->constBegin(); it != p->constEnd(); ++it) {
            obj.insert(it.key(), qvariant_to_josn(it.value()));
        }
        return obj;
    } else if (auto p = get_if<QVariantList>(&variant)) {
        auto arr = QJsonArray();
        for (const auto& item : *p) {
            arr.append(qvariant_to_josn(item));
        }
        return arr;
    } else if (auto p = get_if<QStringList>(&variant); p) {
        return QJsonValue::fromVariant(variant);
    } else if (variant.canConvert<QSequentialIterable>()) {
        auto arr  = QJsonArray();
        auto view = variant.value<QSequentialIterable>();
        for (const auto& item : view) {
            arr.append(qvariant_to_josn(item));
        }
        return arr;
    } else if (variant.canConvert<QAssociativeIterable>()) {
        auto obj  = QJsonObject();
        auto view = variant.value<QAssociativeIterable>();

        QAssociativeIterable::const_iterator       it  = view.begin();
        const QAssociativeIterable::const_iterator end = view.end();
        for (; it != end; ++it) {
            const auto& value = it.value();
            obj.insert(it.key().toString(), qvariant_to_josn(value));
        }
        return obj;
    } else {
        auto type = variant.metaType();
        // try converter
        if (variant.canConvert<QJsonValue>()) {
            return variant.toJsonValue();
        } else if (variant.canConvert<QJsonObject>()) {
            return variant.toJsonObject();
        } else if (variant.canConvert<QJsonArray>()) {
            return variant.toJsonArray();
        }
        //  try gadget
        else if (type.flags() & QMetaType::IsGadget) {
            return qgadget_to_json(variant);
        }
    }
    return QJsonValue::fromVariant(variant);
}
auto kstore::qvariant_from_josn(const QJsonValue&) -> QVariant { return {}; }
