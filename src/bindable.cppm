export module qextra:bindable;
export import rstd.cppstd;
export import qextra.qt;

export template<typename Class, typename T, auto Signal = nullptr>
class ObjectBindableProperty : public QProperty<T> {
    using Base = QProperty<T>;

    struct SignalCallback {
        ObjectBindableProperty* property;

        void operator()() const {
            if constexpr (! rstd::mtp::same_as<decltype(Signal), rstd::nullptr_t>) {
                if constexpr (rstd::mtp::is_invocable<decltype(Signal), Class, T>::value)
                    (property->m_owner->*Signal)(property->valueBypassingBindings());
                else
                    (property->m_owner->*Signal)();
            }
        }
    };

public:
    using typename Base::parameter_type;
    using typename Base::rvalue_ref;

    ObjectBindableProperty(Class* owner): m_owner(owner) {}
    explicit ObjectBindableProperty(const T& value, Class* owner): Base(value), m_owner(owner) {}
    explicit ObjectBindableProperty(T&& value, Class* owner)
        : Base(rstd::move(value)), m_owner(owner) {}

    ObjectBindableProperty& operator=(parameter_type value) {
        this->setValue(value);
        return *this;
    }

    ObjectBindableProperty& operator=(rvalue_ref value) {
        this->setValue(rstd::move(value));
        return *this;
    }

    // Preserve explicit notification after setValueBypassingBindings().
    void notify() { this->bindingData().notifyObservers(this); }

private:
    Class* m_owner;
    QPropertyChangeHandler<SignalCallback> m_signal { *this, SignalCallback { this } };
};
