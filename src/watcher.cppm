export module qextra:watcher;
export import rstd;
export import rstd.cppstd;
export import qt;

using rstd::sync::atomic::Atomic;

export template<typename T>
class QWatcher {
public:
    QWatcher(): m_ptr(nullptr) {}

    QWatcher(T* value): QWatcher() {
        if (value == nullptr) return;

        auto* thread = value->thread();
        m_ptr        = std::shared_ptr<Helper>(new Helper(value, thread), [](Helper* helper) {
            if (QThread::isMainThread() && QThread::currentThread() == helper->owner_thread) {
                auto exec = QThread::currentThread()->property("exec");
                if (! exec.isNull() && ! exec.value<bool>()) {
                    delete helper;
                    return;
                }
            }
            helper->deleteLater();
        });
        m_ptr->moveToThread(thread);
    }

    QWatcher(const QWatcher&)            = default;
    QWatcher& operator=(const QWatcher&) = default;
    QWatcher(QWatcher&&)                 = default;
    QWatcher& operator=(QWatcher&&)      = default;
    ~QWatcher()                          = default;

    T*       operator->() const { return get(); }
    explicit operator bool() const { return get() != nullptr; }

    auto get() const -> T* {
        if (! m_ptr) return nullptr;
        return m_ptr->pointer.load();
    }

    void take_owner() const {
        if (*this) get()->setParent(m_ptr.get());
    }

    operator T*() const { return get(); }

    auto thread() const -> QThread* {
        if (! *this) return nullptr;
        return m_ptr->thread();
    }

private:
    struct Helper : QObject {
        Atomic<T*> pointer;
        QThread*   owner_thread;

        Helper(T* value, QThread* thread): pointer(value), owner_thread(thread) {
            QObject::connect(
                value,
                &QObject::destroyed,
                this,
                [this] {
                    pointer.store(nullptr);
                },
                Qt::DirectConnection);
        }
    };

    std::shared_ptr<Helper> m_ptr;
};
