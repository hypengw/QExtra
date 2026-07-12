module;
#include "QExtra/macro_qt.hpp"

#ifdef Q_MOC_RUN
#    include "QExtra/async.moc"
#endif

export module qextra:async;
export import :executor;
export import :task;
export import :watcher;
export import qt;
export import rstd.cppstd;

using namespace rstd::prelude;

namespace qextra::detail
{
template<typename E>
auto error_string(E&& error) -> QString {
    if constexpr (std::same_as<std::remove_cvref_t<E>, QString>) {
        return std::forward<E>(error);
    } else {
        auto text = rstd::format("{}", std::forward<E>(error));
        return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), text.size());
    }
}

template<typename Fn>
auto own_task(Fn fn) -> qextra::prelude::task<void> {
    co_await fn();
}
} // namespace qextra::detail

class QAsyncResultPrivate;

export class QAsyncResult : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString error READ error NOTIFY errorChanged BINDABLE bindableError FINAL)
    Q_PROPERTY(Status status READ status WRITE setStatus NOTIFY statusChanged BINDABLE
                   bindableStatus FINAL)
    Q_PROPERTY(bool querying READ querying NOTIFY queryingChanged BINDABLE bindableQuerying FINAL)
    Q_PROPERTY(QVariant data READ data NOTIFY dataChanged)
    Q_PROPERTY(
        bool forwardError READ forwardError WRITE setForwardError NOTIFY forwardErrorChanged FINAL)

public:
    QAsyncResult(QObject* parent = nullptr);
    virtual ~QAsyncResult();

    static void initEx(QObject* qt_target, usize worker_threads, void (*)(QStringView));
    static void dropEx();
    static auto qexecutor() -> rstd::async::AnyExecutor;

    enum class Status
    {
        Uninitialized = 0,
        Querying,
        Finished,
        Error
    };
    Q_ENUM(Status);

    auto data() const -> const QVariant&;
    auto data() -> QVariant&;

    auto status() const -> Status;
    auto bindableStatus() -> QBindable<Status>;
    auto querying() const -> bool;
    auto bindableQuerying() -> QBindable<bool>;
    auto error() const -> const QString&;
    auto bindableError() -> QBindable<QString>;
    bool forwardError() const;

    auto use_queue() const -> bool;
    void set_use_queue(bool);

    Q_INVOKABLE virtual void reload();
    void                     set_reload_callback(const std::function<void()>&);

    template<typename Fn>
    void spawn(Fn&& f, const std::source_location loc = std::source_location::current());

    template<typename T, typename TE>
    void from(const Result<T, TE>& exp) {
        if (exp) {
            if constexpr (std::is_base_of_v<QObject, std::decay_t<std::remove_pointer_t<T>>> &&
                          std::is_pointer_v<T>) {
                set_data(exp.value());
            } else {
                set_data(QVariant::fromValue(nullptr));
            }
            setStatus(Status::Finished);
        } else {
            setError(convert_from<QString>(exp.error().what()));
            setStatus(Status::Error);
        }
    }

    Q_SLOT void cancel();
    Q_SLOT void setStatus(Status);
    Q_SLOT void setError(const QString&);
    Q_SLOT void setForwardError(bool);
    Q_SLOT void set_data(const QVariant&);
    Q_SLOT void hold(QStringView, QObject*);

    Q_SIGNAL void dataChanged();
    Q_SIGNAL void statusChanged(Status);
    Q_SIGNAL void queryingChanged(bool);
    Q_SIGNAL void errorChanged(QString);
    Q_SIGNAL void forwardErrorChanged();
    Q_SIGNAL void finished();
    Q_SIGNAL void errorOccurred(QString);

    template<typename T, typename Err>
    void check(const Result<T, Err>& res) {
        if (! res) {
            setError(QString::fromStdString(res.error().what()));
            setStatus(Status::Error);
        }
    }

private:
    void push(std::function<qextra::prelude::task<void>()>, const std::source_location& loc);
    void start(qextra::prelude::task<void>, const std::source_location& loc, bool queued);

    QScopedPointer<QAsyncResultPrivate> d_ptr;
    Q_DECLARE_PRIVATE(QAsyncResult)
};

export template<typename T, typename Self>
class QAsyncResultExtra {
public:
    using value_type = std::conditional_t<std::is_base_of_v<QObject, T>, std::add_pointer_t<T>, T>;
    using const_reference_value_type =
        std::conditional_t<std::is_base_of_v<QObject, T>, std::add_pointer_t<T>,
                           std::add_lvalue_reference_t<std::add_const_t<T>>>;
    auto tdata() const {
        return static_cast<const QAsyncResult*>(static_cast<const Self*>(this))
            ->data()
            .template value<value_type>();
    }
    auto tdata() {
        return static_cast<QAsyncResult*>(static_cast<Self*>(this))
            ->data()
            .template value<value_type>();
    }
    void set_tdata(const_reference_value_type val) {
        auto self = static_cast<Self*>(this);
        if constexpr (std::is_base_of_v<QObject, T>) {
            auto old = tdata();
            self->set_data(QVariant::fromValue(val));
            if (old) {
                old->deleteLater();
            }
        } else {
            self->set_data(QVariant::fromValue(val));
        }
    }

    template<typename U, typename E = typename std::remove_reference_t<U>::error_type>
        requires(std::convertible_to<U, Result<T, E>> || std::convertible_to<U, Result<T&, E>>)
    void set(U&& res) {
        auto self = static_cast<Self*>(this);
        if (res) {
            set_tdata(*res);
            self->setStatus(QAsyncResult::Status::Finished);
        } else {
            self->setError(qextra::detail::error_string(res.unwrap_err_unchecked()));
            self->setStatus(QAsyncResult::Status::Error);
        }
    }

    template<typename U, typename F>
    void inspect_set(U&& res, F&& f) {
        auto self = static_cast<Self*>(this);
        if (res) {
            res.inspect(std::forward<F>(f));
            self->setStatus(QAsyncResult::Status::Finished);
        } else {
            self->setError(qextra::detail::error_string(res.unwrap_err_unchecked()));
            self->setStatus(QAsyncResult::Status::Error);
        }
    }
};

template<typename Fn>
void QAsyncResult::spawn(Fn&& f, const std::source_location loc) {
    if (use_queue()) {
        push(std::forward<Fn>(f), loc);
    } else {
        start(qextra::detail::own_task(std::forward<Fn>(f)), loc, false);
    }
}
