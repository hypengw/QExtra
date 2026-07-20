module;
#include "QExtra/async.moc.h"
#include "QExtra/macro_qt.hpp"

module qextra;
import :async;
import :bindable;
import rstd.cppstd;

using CancelSender = rstd::async::oneshot::Sender<rstd::empty>;

struct GlobalEx {
    rstd::async::AnyExecutor qex;
    rstd::async::Runtime     runtime;
    void (*error_callback)(QStringView);
};

auto global_ex() -> std::optional<GlobalEx>& {
    static std::optional<GlobalEx> value;
    return value;
}

class QAsyncResultPrivate {
public:
    using Status = QAsyncResult::Status;

    explicit QAsyncResultPrivate(QAsyncResult* parent)
        : m_p(parent),
          m_forward_error(true),
          m_data(QVariant::fromValue(nullptr)),
          m_use_queue(false),
          m_queue_exec_mark(false),
          m_querying(false, parent),
          m_status(Status::Uninitialized, parent),
          m_error(parent) {}

    QAsyncResult*         m_p;
    bool                  m_forward_error;
    QVariant              m_data;
    std::function<void()> m_cb;

    std::map<QString, QObject*, std::less<>> m_hold;
    std::optional<CancelSender>              m_cancel;
    quint64                                  m_generation { 0 };

    bool m_use_queue;
    bool m_queue_exec_mark;
    std::deque<std::tuple<std::function<qextra::prelude::task<void>()>, std::source_location>>
        m_queue;

    ObjectBindableProperty<QAsyncResult, bool, &QAsyncResult::queryingChanged> m_querying;
    ObjectBindableProperty<QAsyncResult, Status, &QAsyncResult::statusChanged> m_status;
    ObjectBindableProperty<QAsyncResult, QString, &QAsyncResult::errorChanged> m_error;

    void try_run() {
        if (m_queue.empty() || m_queue_exec_mark || ! m_use_queue) return;

        auto [work, loc] = rstd::move(m_queue.front());
        m_queue.pop_front();
        m_queue_exec_mark = true;
        m_p->setStatus(Status::Querying);
        m_p->start(qextra::own_task(rstd::move(work)), loc, true);
    }

    void handle_queue() {
        m_queue_exec_mark = false;
        try_run();
    }
};

template<typename Finish>
auto monitor_task(rstd::async::AbortOnDropHandle<void>        work,
                  rstd::async::oneshot::Receiver<rstd::empty> cancellation,
                  rstd::async::AnyExecutor qt, Finish finish) -> qextra::prelude::task<void> {
    auto outcome = co_await rstd::async::select(
        rstd::async::timeout(rstd::move(work), rstd::time::Duration::from_secs(rstd::u64(180))),
        rstd::move(cancellation));

    if (! co_await qt) co_return;
    finish(rstd::move(outcome));
}

QAsyncResult::QAsyncResult(QObject* parent): QObject(parent), d_ptr(new QAsyncResultPrivate(this)) {
    Q_D(QAsyncResult);
    connect(this, &QAsyncResult::statusChanged, this, [this](Status status) {
        if (status == Status::Finished) {
            finished();
        } else if (status == Status::Error) {
            errorOccurred(error());
        }
        if (forwardError() && status == Status::Error) {
            auto& global = global_ex();
            if (global && global->error_callback != nullptr) {
                global->error_callback(error());
            }
        }
    });

    d->m_querying.setBinding([d] {
        return d->m_status.value() == Status::Querying;
    });
}

QAsyncResult::~QAsyncResult() { cancel(); }

void QAsyncResult::hold(QStringView name, QObject* object) {
    Q_D(QAsyncResult);
    if (object == nullptr) return;

    object->setParent(this);
    if (auto it = d->m_hold.find(name); it != d->m_hold.end()) {
        it->second->deleteLater();
        it->second = object;
    } else {
        d->m_hold.insert({ name.toString(), object });
    }
}

void QAsyncResult::initEx(QObject* qt_target, usize worker_threads,
                          void (*error_callback)(QStringView)) {
    auto runtime = rstd::async::RuntimeBuilder::multi_thread()
                       .worker_threads(worker_threads)
                       .enable_all()
                       .build()
                       .unwrap();
    global_ex().emplace(GlobalEx {
        rstd::async::AnyExecutor::from_executor(QtExecutor { qt_target }),
        rstd::move(runtime),
        error_callback,
    });
}

void QAsyncResult::dropEx() { global_ex().reset(); }

auto QAsyncResult::qexecutor() -> rstd::async::AnyExecutor { return global_ex()->qex.clone(); }

auto QAsyncResult::runtime_handle() -> rstd::async::RuntimeHandle {
    return global_ex()->runtime.handle();
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

void QAsyncResult::setStatus(Status value) {
    Q_D(QAsyncResult);
    d->m_status = value;
}

void QAsyncResult::reload() {
    Q_D(const QAsyncResult);
    if (d->m_cb) d->m_cb();
}

void QAsyncResult::set_reload_callback(const std::function<void()>& callback) {
    Q_D(QAsyncResult);
    d->m_cb = callback;
}

auto QAsyncResult::error() const -> const QString& {
    Q_D(const QAsyncResult);
    return d->m_error.value();
}

auto QAsyncResult::bindableError() -> QBindable<QString> {
    Q_D(QAsyncResult);
    return &(d->m_error);
}

void QAsyncResult::setError(const QString& value) {
    Q_D(QAsyncResult);
    d->m_error = value;
}

bool QAsyncResult::forwardError() const {
    Q_D(const QAsyncResult);
    return d->m_forward_error;
}

void QAsyncResult::setForwardError(bool value) {
    Q_D(QAsyncResult);
    if (d->m_forward_error == value) return;
    d->m_forward_error = value;
    emit forwardErrorChanged();
}

void QAsyncResult::cancel() {
    Q_D(QAsyncResult);
    if (d->m_cancel) {
        (void)d->m_cancel->send(rstd::empty {});
        d->m_cancel.reset();
    }
}

auto QAsyncResult::use_queue() const -> bool {
    Q_D(const QAsyncResult);
    return d->m_use_queue;
}

void QAsyncResult::set_use_queue(bool value) {
    Q_D(QAsyncResult);
    d->m_use_queue = value;
}

auto QAsyncResult::data() const -> const QVariant& {
    Q_D(const QAsyncResult);
    return d->m_data;
}

auto QAsyncResult::data() -> QVariant& {
    Q_D(QAsyncResult);
    return d->m_data;
}

void QAsyncResult::set_data(const QVariant& value) {
    Q_D(QAsyncResult);
    if (d->m_data != value) {
        d->m_data = value;
        dataChanged();
    }
    if (auto* object = d->m_data.value<QObject*>(); object != nullptr && object->parent() != this) {
        object->setParent(this);
    }
}

void QAsyncResult::push(std::function<qextra::prelude::task<void>()> work,
                        const std::source_location&                  loc) {
    Q_D(QAsyncResult);
    d->m_queue.emplace_back(rstd::move(work), loc);
    d->try_run();
}

void QAsyncResult::start(qextra::prelude::task<void> work, const std::source_location& loc,
                         bool queued) {
    Q_D(QAsyncResult);
    cancel();

    auto channel      = rstd::async::oneshot::channel<rstd::empty>();
    auto cancellation = rstd::move(channel.get<1>());
    d->m_cancel.emplace(rstd::move(channel.get<0>()));
    auto generation = ++d->m_generation;
    auto self       = QWatcher<QAsyncResult> { this };
    auto qt         = qexecutor();
    auto work_handle =
        rstd::async::AbortOnDropHandle<void> { global_ex()->runtime.spawn(rstd::move(work)) };

    auto finish = [self, generation, queued, loc](auto outcome) mutable {
        if (! self) return;

        auto* state = self->d_func();
        if (state->m_generation == generation) {
            state->m_cancel.reset();
        }

        if (outcome.is_left()) {
            auto timed = rstd::move(outcome).unwrap_left();
            if (timed.is_err()) {
                self->setError(QStringLiteral("Operation timed out"));
                self->setStatus(Status::Error);
                qCritical() << loc.file_name() << "Operation timed out";
            }
        }

        if (queued) state->handle_queue();
    };

    (void)global_ex()->runtime.spawn(monitor_task(
        rstd::move(work_handle), rstd::move(cancellation), rstd::move(qt), rstd::move(finish)));
}

#include "QExtra/async.moc.cpp"
