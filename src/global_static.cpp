module;
#undef assert
#include <rstd/macro.hpp>
module qextra;
import :global_static;

struct GlobalStatic::HolderImpl {
    std::atomic<void*>         instance { nullptr };
    std::function<void(void*)> deleter;

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
    std::map<std::string, std::shared_ptr<HolderImpl>, std::less<>> instances;
};

GlobalStatic::GlobalStatic(): d_ptr(std::make_unique<Private>()) {}
GlobalStatic::~GlobalStatic() { reset(); }
auto GlobalStatic::instance() -> GlobalStatic* {
    static GlobalStatic theGlobalStatic;
    return &theGlobalStatic;
}

auto GlobalStatic::data(HolderImpl& holder) -> void* {
    assert(holder.instance);
    return holder.instance;
}

auto GlobalStatic::add_impl(std::string_view name, void* instance,
                            std::function<void(void*)> deleter)
    -> std::shared_ptr<HolderImpl> {
    auto holder      = std::make_shared<HolderImpl>();
    holder->instance = instance;
    holder->deleter  = deleter;
    d_ptr->instances.insert({ std::string(name), holder });
    return holder;
}
void GlobalStatic::reset() {
    for (auto& el : d_ptr->instances) {
        el.second->reset();
    }
    d_ptr->instances.clear();
}
