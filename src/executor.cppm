module;
#include <QMetaObject>
#include <QObject>
#include <QPointer>

export module qextra:executor;
export import rstd;
export import rstd.cppstd;
export import qt;

export class QtExecutor {
public:
    explicit QtExecutor(QObject* target): m_target(target) {}

    auto post_job(rstd::async::ExecutorJob job) -> bool {
        auto* target = m_target.data();
        if (target == nullptr) return false;

        auto owned = std::make_shared<rstd::async::ExecutorJob>(rstd::move(job));
        return QMetaObject::invokeMethod(
            target,
            [owned = rstd::move(owned)]() mutable {
                owned->run();
            },
            Qt::QueuedConnection);
    }

    auto is_closed() -> bool { return m_target.isNull(); }

private:
    QPointer<QObject> m_target;
};

template<>
struct rstd::Impl<rstd::async::Executor, QtExecutor>
    : rstd::LinkClassMethod<rstd::async::Executor, QtExecutor> {};
