module;
#include "QExtra/macro_qt.hpp"
#include "QExtra/async.moc.h"
#undef assert
#include <rstd/macro.hpp>
module qextra;
import :bindable;
import :async;
import asio;

struct GlobalEx {
    QtExecutor                       qex;
    asio::thread_pool::executor_type pex;
    void (*cb)(QStringView);
};

auto global_ex(rstd::Option<GlobalEx> in = {}) -> rstd::Option<GlobalEx>& {
    static rstd::Option<GlobalEx> the { rstd::move(in) };
    return the;
}

class QAsyncResultPrivate {
public:
    using Status = QAsyncResult::Status;
    QAsyncResultPrivate(QAsyncResult* p)
        : m_p(p),
          m_forward_error(true),
          m_data(QVariant::fromValue(nullptr)),
          m_use_queue(false),
          m_queue_exec_mark(false),
          m_querying(false, p),
          m_status(Status::Uninitialized, p),
          m_error(p) {}
    QAsyncResult*            m_p;
    bool                     m_forward_error;
    QVariant                 m_data;
    std::function<void()> m_cb;

    WatchDog                                       m_wdog;
    std::map<QString, QObject*, std::less<>> m_hold;

    bool m_use_queue;
    bool m_queue_exec_mark;
    std::deque<std::tuple<std::function<asio::awaitable<void>()>, std::source_location>>
        m_queue;

    ObjectBindableProperty<QAsyncResult, bool, &QAsyncResult ::queryingChanged> m_querying;
    ObjectBindableProperty<QAsyncResult, Status, &QAsyncResult ::statusChanged> m_status;
    ObjectBindableProperty<QAsyncResult, QString, &QAsyncResult::errorChanged>  m_error;

    void try_run() {
        if (m_queue.empty() || m_queue_exec_mark || ! m_use_queue) return;

        auto [f, loc] = m_queue.front();
        m_queue.pop_front();

        auto                   ex = asio::make_strand(m_p->pool_executor());
        QWatcher<QAsyncResult> self { m_p };
        auto                   main_ex { m_p->get_executor() };
        auto                   alloc = asio::recycling_allocator<void>();

        m_p->setStatus(Status::Querying);
        m_queue_exec_mark = true;
        asio::co_spawn(ex,
                       m_p->watch_dog().watch(
                           ex,
                           [f = std::move(f)]() -> asio::awaitable<void> {
                               co_await f();
                           },
                           asio::chrono::minutes(3),
                           alloc),
                       asio::bind_allocator(alloc, [self, main_ex, loc](std::exception_ptr p) {
                           if (p) {
                               try {
                                   std::rethrow_exception(p);
                               } catch (const std::exception& e) {
                                   std::string e_str = e.what();
                                   asio::post(main_ex, [self, e_str]() {
                                       if (self) {
                                           self->setError(QString::fromStdString(e_str));
                                           self->setStatus(Status::Error);
                                       }
                                   });
                                   qCritical() << loc.file_name();
                                   qCritical() << e_str;
                               }
                           }

                           asio::post(main_ex, [self] {
                               if (self) {
                                   self->d_func()->handle_queue();
                               }
                           });
                       }));
    }

    void handle_queue() {
        m_queue_exec_mark = false;
        try_run();
    }
};

QAsyncResult::QAsyncResult(QObject* parent): QObject(parent), d_ptr(new QAsyncResultPrivate(this)) {
    Q_D(QAsyncResult);
    connect(this, &QAsyncResult::statusChanged, this, [this](Status s) {
        if (s == Status::Finished) {
            finished();
        } else if (s == Status::Error) {
            errorOccurred(error());
        }
        if (forwardError() && s == Status::Error) {
            global_ex()->cb(error());
        }
    });

    d->m_querying.setBinding([d] {
        return d->m_status.value() == Status::Querying;
    });
}

QAsyncResult::~QAsyncResult() {}

void QAsyncResult::hold(QStringView name, QObject* o) {
    Q_D(QAsyncResult);
    if (o != nullptr) {
        o->setParent(this);
        if (auto it = d->m_hold.find(name); it != d->m_hold.end()) {
            it->second->deleteLater();
            it->second = o;
        } else {
            d->m_hold.insert({ name.toString(), o });
        }
    }
}

void QAsyncResult::initEx(QtExecutor qex, asio::thread_pool::executor_type pex,
                          void (*cb)(QStringView)) {
    global_ex(rstd::Some(GlobalEx { qex, pex, cb }));
}
void QAsyncResult::dropEx() { global_ex().take(); }

auto QAsyncResult::qexecutor() -> QtExecutor& { return global_ex()->qex; }

auto QAsyncResult::pool_executor() const -> asio::thread_pool::executor_type {
    return global_ex()->pex;
}

auto QAsyncResult::status() const -> Status {
    Q_D(const QAsyncResult);
    return d->m_status.value();
}
auto QAsyncResult::bindableStatus() -> QBindable<Status> {
    Q_D(QAsyncResult);
    return &(d->m_status);
}
auto QAsyncResult::querying() const -> bool {
    Q_D(const QAsyncResult);
    return d->m_querying.value();
}
auto QAsyncResult::bindableQuerying() -> QBindable<bool> {
    Q_D(QAsyncResult);
    return &(d->m_querying);
}

void QAsyncResult::setStatus(Status v) {
    Q_D(QAsyncResult);
    d->m_status = v;
}
void QAsyncResult::reload() {
    Q_D(const QAsyncResult);
    if (d->m_cb) {
        d->m_cb();
    }
}
void QAsyncResult::set_reload_callback(const std::function<void()>& f) {
    Q_D(QAsyncResult);
    d->m_cb = f;
}

auto QAsyncResult::error() const -> const QString& {
    Q_D(const QAsyncResult);
    return d->m_error.value();
}
auto QAsyncResult::bindableError() -> QBindable<QString> {
    Q_D(QAsyncResult);
    return &(d->m_error);
}
void QAsyncResult::setError(const QString& v) {
    Q_D(QAsyncResult);
    d->m_error = v;
}

bool QAsyncResult::forwardError() const {
    Q_D(const QAsyncResult);
    return d->m_forward_error;
}
void QAsyncResult::setForwardError(bool v) {
    Q_D(QAsyncResult);
    if (d->m_forward_error != v) {
        d->m_forward_error = v;
        emit forwardErrorChanged();
    }
}
void QAsyncResult::cancel() {
    Q_D(QAsyncResult);
    d->m_wdog.cancel();
}
auto QAsyncResult::get_executor() -> QtExecutor& {
    return qexecutor();
}
auto QAsyncResult::use_queue() const -> bool {
    Q_D(const QAsyncResult);
    return d->m_use_queue;
}
void QAsyncResult::set_use_queue(bool v) {
    Q_D(QAsyncResult);
    d->m_use_queue = v;
}

auto QAsyncResult::watch_dog() -> WatchDog& {
    Q_D(QAsyncResult);
    return d->m_wdog;
}

auto QAsyncResult::data() const -> const QVariant& {
    Q_D(const QAsyncResult);
    return d->m_data;
}

auto QAsyncResult::data() -> QVariant& {
    Q_D(QAsyncResult);
    return d->m_data;
}

void QAsyncResult::set_data(const QVariant& v) {
    Q_D(QAsyncResult);
    if (d->m_data != v) {
        d->m_data = v;
        dataChanged();
    }
    if (auto obj = d->m_data.value<QObject*>(); obj != nullptr) {
        if (obj->parent() != this) {
            obj->setParent(this);
        }
    }
}
void QAsyncResult::push(std::function<asio::awaitable<void>()> in,
                        const std::source_location&         loc) {
    Q_D(QAsyncResult);
    d->m_queue.emplace_back(in, loc);

    d->try_run();
}

usize QAsyncResult::size() const {
    Q_D(const QAsyncResult);
    return d->m_queue.size();
}

#include "QExtra/async.moc.cpp"