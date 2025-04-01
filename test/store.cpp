#include <format>
#include <gtest/gtest.h>

#include "meta_model/qgadget_list_model.hpp"

struct Model {
    Q_GADGET

    Q_PROPERTY(int uid MEMBER uid)
public:
    int uid;
    int age { 18 };
};

struct StoreNotify {};

struct Store {
    struct Item {
        Model   m;
        int64_t count { 1 };
    };
    struct Inner {
        std::unordered_map<int, Item>               map;
        int64_t                                     serial { 0 };
        std::map<int64_t, std::function<void(int)>> callbacks;
    };
    std::shared_ptr<Inner> inner;

    Store(): inner(std::make_shared<Inner>()) {}

    auto store_query(int k) -> Model* {
        auto it = inner->map.find(k);
        if (it != inner->map.end()) {
            return std::addressof(it->second.m);
        }
        return nullptr;
    }
    auto store_query(int k) const -> Model const* {
        auto it = inner->map.find(k);
        if (it != inner->map.end()) {
            return std::addressof(it->second.m);
        }
        return nullptr;
    }
    void store_insert(Model m, bool new_one, int64_t handle) {
        if (auto it = inner->map.find(m.uid); it != inner->map.end()) {
            it->second.m = m;
            if (new_one) ++(it->second.count);
        } else {
            inner->map.insert({ m.uid, Item { m } });
        }
    }
    void store_remove(int k) {
        if (auto it = inner->map.find(k); it != inner->map.end()) {
            auto count = --(it->second.count);
            if (count == 0) inner->map.erase(count);
        }
    }

    int64_t store_reg_notify(std::function<void(int)>) { return ++(inner->serial); }
    void    store_unreg_notify(int64_t) {}
};

template<>
struct meta_model::ItemTrait<Model> {
    using key_type   = int;
    using store_type = Store;
    static auto key(meta_model::param_type<Model> m) { return m.uid; }
};

struct ListModel : meta_model::QGadgetListModel<Model, meta_model::QMetaListStore::Share> {
    Q_OBJECT
public:
    using base_type = meta_model::QGadgetListModel<Model, meta_model::QMetaListStore::Share>;
    ListModel(QObject* p = nullptr): base_type(p) {}
};

TEST(Store, Basic) {
    Store     store;
    ListModel m;
    m.set_store(store);
    m.insert(0, std::array { Model { 1 }, Model { 2 } });

    EXPECT_EQ(m.at(0).uid, 1);
    EXPECT_EQ(m.at(1).uid, 2);
}

TEST(Store, Share) {
    Store     store;
    ListModel m;
    ListModel n;
    m.set_store(store);
    n.set_store(store);
    m.insert(0, std::array { Model { 1 }, Model { 2 } });

    EXPECT_EQ(m.at(0).age, 18);
    EXPECT_EQ(m.at(1).age, 18);

    n.insert(0, std::array { Model { 1, 10 }, Model { 2, 20 } });

    EXPECT_EQ(m.at(0).age, 10);
    EXPECT_EQ(m.at(1).age, 20);
}

#include "store.moc"