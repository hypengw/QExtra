module;
#include <qtversionchecks.h>
export module qextra:helper;
// export import qcm.helper;
export import rstd.cppstd;
export import qextra.qt;

using namespace Qt::StringLiterals;

export namespace helper
{

inline bool is_floating_point_metatype_id(int id) {
    switch (id) {
    case QMetaType::Float16:
    case QMetaType::Float:
    case QMetaType::Double: return true;
    default: return false;
    }
}

inline bool is_integer_metatype_id(int id) {
    switch (id) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Bool: return true;
    default: return false;
    }
}

inline bool is_numeric_metatype_id(int id) {
    switch (id) {
    case QMetaType::Float:
    case QMetaType::Double: return true;
    default: return is_integer_metatype_id(id);
    }
}

inline bool is_numeric_metatype(QMetaType type) { return is_numeric_metatype_id(type.id()); }
} // namespace helper

/*
template<typename T, typename F>
    requires std::ranges::range<F> && convertable<T, std::ranges::range_value_t<F>>
struct Convert<QList<T>, F> {
    static void from(QList<T>& out, const F& f) {
        using from_value_type = std::ranges::range_value_t<F>;
        out.clear();
        std::transform(std::ranges::begin(f),
                          std::ranges::end(f),
                          std::back_inserter(out),
                          [](const from_value_type& v) {
                              return convert_from<T>(v);
                          });
    }
};

template<typename T>
    requires rstd::mtp::convertible_to<T, std::string_view> ||
             convertable<std::string_view, T> || convertable<std::string, T>
struct Convert<QString, T> {
    static void from(QString& out, const T& in) {
        if constexpr (rstd::mtp::convertible_to<T, std::string_view>) {
            auto sv = std::string_view(in);
            out     = QString::fromUtf8(sv.data(), sv.size());
        } else if constexpr (convertable<std::string_view, T>) {
            auto sv = convert_from<std::string_view>(in);
            out     = QString::fromUtf8(sv.data(), sv.size());
        } else {
            out = QString::fromStdString(convert_from<std::string>(in));
        }
    }
};

template<>
struct Convert<std::string, QString> {
    static void from(std::string& out, const QString& in) { out = in.toStdString(); }
};

template<>
struct Convert<std::string, QStringView> {
    static void from(std::string& out, QStringView in) { out = in.toString().toStdString(); }
};

template<>
struct Convert<std::string, QLatin1String> {
    static void from(std::string& out, QLatin1String in) { out = in.toString().toStdString(); }
};

template<>
struct Convert<std::string, QUtf8StringView> {
    static void from(std::string& out, QUtf8StringView in) {
        out = std::string_view { in.data(), (usize)in.size() };
    }
};

template<>
struct Convert<std::string, QAnyStringView> {
    static void from(std::string& out, QAnyStringView in) {
        in.visit([&out](auto v) {
            Convert<std::string, decltype(v)>::from(out, v);
        });
    }
};


template<typename T>
struct Convert<std::optional<T>, QVariant> {
    using out_type = std::optional<T>;
    using in_type  = QVariant;
    static void from(out_type& o, const in_type& in) {
        o = in.canConvert<T>() ? out_type(in.value<T>()) : std::nullopt;
    }
};

template<typename T>
struct Convert<QVariant, std::optional<T>> {
    using out_type = QVariant;
    using in_type  = std::optional<T>;
    static void from(out_type& o, const in_type& in) {
        if (in) {
            o = out_type::fromValue(in.value());
        }
    }
};

*/

export namespace qextra
{
inline auto to_qstring(const rstd::string::String& str) -> QString {
    return QString::fromUtf8(
        reinterpret_cast<const char*>(str.data()),
        static_cast<decltype(QString {}.size())>(str.size().to_primitive()));
}
} // namespace qextra

export template<>
struct rstd::Impl<rstd::fmt::Display, QString> : rstd::ImplBase<QString> {
    auto fmt(rstd::fmt::Formatter& f) const -> bool {
        auto str = this->self().toStdString();
        return f.write_raw(str.data(), rstd::size_t(str.size()));
    }
};

export template<>
struct rstd::Impl<rstd::fmt::Display, QStringView> : rstd::ImplBase<QStringView> {
    auto fmt(rstd::fmt::Formatter& f) const -> bool {
        auto str = this->self().toString().toStdString();
        return f.write_raw(str.data(), rstd::size_t(str.size()));
    }
};

export template<>
struct rstd::Impl<rstd::fmt::Display, QLatin1String> : rstd::ImplBase<QLatin1String> {
    auto fmt(rstd::fmt::Formatter& f) const -> bool {
        auto str = this->self().toString().toStdString();
        return f.write_raw(str.data(), rstd::size_t(str.size()));
    }
};

export template<>
struct rstd::Impl<rstd::fmt::Display, QUtf8StringView> : rstd::ImplBase<QUtf8StringView> {
    auto fmt(rstd::fmt::Formatter& f) const -> bool {
        return f.write_raw(
            this->self().data(), rstd::size_t(static_cast<std::size_t>(this->self().size())));
    }
};

export template<>
struct rstd::Impl<rstd::fmt::Display, QAnyStringView> : rstd::ImplBase<QAnyStringView> {
    auto fmt(rstd::fmt::Formatter& f) const -> bool {
        bool out;
        this->self().visit([&f, &out](auto v) {
            out = rstd::as<rstd::fmt::Display>(v).fmt(f);
        });
        return out;
    }
};

export template<>
struct rstd::Impl<rstd::convert::From<rstd::string::String>, QString> {
    static auto from(const rstd::string::String& str) {
        return qextra::to_qstring(str);
    }
};
export template<>
struct rstd::Impl<rstd::convert::From<std::string>, QString> {
    static auto from(std::string str) { return QString::fromStdString(rstd::move(str)); }
};
export template<>
struct rstd::Impl<rstd::convert::From<std::string>, QUrl> {
    static auto from(std::string str) { return QString::fromStdString(rstd::move(str)); }
};

export template<>
struct rstd::Impl<rstd::convert::From<std::string>, QStringView> {
    static auto from(std::string str) { return QString::fromStdString(rstd::move(str)); }
};

export template<>
struct rstd::Impl<rstd::convert::From<rstd::string::String>, QUrl> {
    static auto from(const rstd::string::String& str) -> QUrl {
        return qextra::to_qstring(str);
    }
};

#if (QT_VERSION < QT_VERSION_CHECK(6, 8, 0))
export inline std::strong_ordering operator<=>(const QString& a, const QString& b) {
    return a < b ? std::strong_ordering::less
                 : (a == b ? std::strong_ordering::equal : std::strong_ordering::greater);
}
#endif
