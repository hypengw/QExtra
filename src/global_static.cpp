module;
#undef assert
#include <rstd/macro.hpp>
module qextra;
import :global_static;

struct GlobalStatic::HolderImpl {
    cppstd::atomic<void*>         instance { nullptr };
    cppstd::function<void(void*)> deleter;

    void reset() {
        if (deleter && instance) {
            deleter(instance);
            deleter  = {};
            instance = nullptr;
        }
    }

    HolderImpl() {}
    ~HolderImpl() { reset(); }
};

class GlobalStatic::Private {
public:
    cppstd::map<cppstd::string, cppstd::shared_ptr<HolderImpl>, cppstd::less<>> instances;
};

GlobalStatic::GlobalStatic(): d_ptr(cppstd::make_unique<Private>()) {}
GlobalStatic::~GlobalStatic() { reset(); }
auto GlobalStatic::instance() -> GlobalStatic* {
    static GlobalStatic theGlobalStatic;
    return &theGlobalStatic;
}

auto GlobalStatic::data(HolderImpl& holder) -> void* {
    assert(holder.instance);
    return holder.instance;
}

auto GlobalStatic::add_impl(cppstd::string_view name, void* instance,
                            cppstd::function<void(void*)> deleter)
    -> cppstd::shared_ptr<HolderImpl> {
    auto holder      = cppstd::make_shared<HolderImpl>();
    holder->instance = instance;
    holder->deleter  = deleter;
    d_ptr->instances.insert({ cppstd::string(name), holder });
    return holder;
}
void GlobalStatic::reset() {
    for (auto& el : d_ptr->instances) {
        el.second->reset();
    }
    d_ptr->instances.clear();
}
