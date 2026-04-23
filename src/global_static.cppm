export module qextra:global_static;
export import rstd.cppstd;


export class GlobalStatic {
public:
    GlobalStatic();
    ~GlobalStatic();
    static auto instance() -> GlobalStatic*;

    struct HolderImpl;

    template<typename T>
    struct Holder {
        Holder(cppstd::shared_ptr<HolderImpl> d): d_ptr(d) {}
        ~Holder() = default;
        auto operator->() -> HolderImpl* { return d_ptr.get(); };
             operator T*() { return static_cast<T*>(data(*d_ptr)); };

    private:
        cppstd::shared_ptr<HolderImpl> d_ptr;
    };

    template<typename T>
    auto add(cppstd::string_view name, T* instance, cppstd::function<void(T*)> deleter)
        -> Holder<T> {
        return add_impl(name, static_cast<void*>(instance), [deleter](void* p) {
            deleter(static_cast<T*>(p));
        });
    }

    void reset();

private:
    auto add_impl(cppstd::string_view name, void* instance,
                  cppstd::function<void(void*)> deleter) -> cppstd::shared_ptr<HolderImpl>;
    static auto data(HolderImpl&) -> void*;

    class Private;
    cppstd::unique_ptr<Private> d_ptr;
};
