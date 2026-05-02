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

auto qgadget_from_json(const QMetaType& type, const QJsonObject& obj) -> QVariant {
    QVariant gadget(type, nullptr);
    auto     meta = type.metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        auto prop = meta->property(i);
        if (prop.metaType().flags() & QMetaType::IsPointer) {
            // ignore
        } else if (prop.isWritable() && prop.isStored()) {
            auto name = QString::fromUtf8(prop.name());
            auto v    = obj.value(name);
            if (! v.isUndefined()) {
                QVariant propVar = kstore::qvariant_from_josn(prop.metaType(), v);
                prop.writeOnGadget(gadget.data(), propVar);
            }
        }
    }
    return gadget;
} // namespace
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
        // try sequential iterable
        else if (variant.canConvert<QSequentialIterable>()) {
            if (variant.canConvert<QString>()) {
                return variant.toString();
            }
            auto arr  = QJsonArray();
            auto view = variant.value<QSequentialIterable>();
            for (const auto& item : view) {
                arr.append(qvariant_to_josn(item));
            }
            return arr;
        }
    }
    return QJsonValue::fromVariant(variant);
}

auto kstore::qvariant_from_josn(const QMetaType& type, const QJsonValue& value) -> QVariant {
    if (QMetaType::hasRegisteredConverterFunction(QMetaType::fromType<QJsonValue>(), type)) {
        QVariant out(type, nullptr);
        QMetaType::convert(QMetaType::fromType<QJsonValue>(), &value, type, out.data());
        return out;
    }
    if (value.isObject()) {
        if (type.id() == QMetaType::QVariantHash) {
            QVariantHash map;
            QJsonObject  obj = value.toObject();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                map.insert(it.key(),
                           qvariant_from_josn(QMetaType::fromType<QVariant>(), it.value()));
            }
            return map;
        } else if (type.id() == QMetaType::QVariantMap) {
            QVariantMap map;
            QJsonObject obj = value.toObject();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                map.insert(it.key(),
                           qvariant_from_josn(QMetaType::fromType<QVariant>(), it.value()));
            }
            return map;
        }
        if (QMetaType::hasRegisteredConverterFunction(QMetaType::fromType<QJsonObject>(), type)) {
            QVariant out(type, nullptr);
            auto     obj = value.toObject();
            QMetaType::convert(QMetaType::fromType<QJsonObject>(), &obj, type, out.data());
            return out;
        }
        if (type.flags() & QMetaType::IsGadget) {
            auto obj = value.toObject();
            return qgadget_from_json(type, obj);
        }
    } else if (value.isArray()) {
        if (type.id() == QMetaType::QVariantList) {
            QVariantList list;
            QJsonArray   arr = value.toArray();
            for (const auto& v : arr) {
                list.append(qvariant_from_josn(QMetaType::fromType<QVariant>(), v));
            }
            return list;
        }
        if (type.id() == QMetaType::QStringList) {
            QStringList list;
            QJsonArray  arr = value.toArray();
            for (const auto& v : arr) {
                list.append(v.toString());
            }
            return list;
        }
        if (QMetaType::hasRegisteredConverterFunction(QMetaType::fromType<QJsonArray>(), type)) {
            QVariant out(type, nullptr);
            auto     arr = value.toArray();
            QMetaType::convert(QMetaType::fromType<QJsonArray>(), &arr, type, out.data());
            return out;
        }
    }

    auto variant = value.toVariant();
    if (variant.metaType().id() == type.id()) {
        return variant;
    } else if (variant.convert(type)) {
        return variant;
    }

    return QVariant {};
}
