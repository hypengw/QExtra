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
        Holder(std::shared_ptr<HolderImpl> d): d_ptr(d) {}
        ~Holder() = default;
        auto operator->() -> HolderImpl* { return d_ptr.get(); };
             operator T*() { return static_cast<T*>(data(*d_ptr)); };

    private:
        std::shared_ptr<HolderImpl> d_ptr;
    };

    template<typename T>
    auto add(std::string_view name, T* instance, std::function<void(T*)> deleter)
        -> Holder<T> {
        return add_impl(name, static_cast<void*>(instance), [deleter](void* p) {
            deleter(static_cast<T*>(p));
        });
    }

    void reset();

private:
    auto add_impl(std::string_view name, void* instance,
                  std::function<void(void*)> deleter) -> std::shared_ptr<HolderImpl>;
    static auto data(HolderImpl&) -> void*;

    class Private;
    std::unique_ptr<Private> d_ptr;
};
