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

template<>
struct meta_model::ItemTrait<Model> {
    using key_type   = int;
    using store_type = meta_model::ShareStore<Model>;
    static auto key(meta_model::param_type<Model> m) { return m.uid; }
};

struct ListModel : meta_model::QGadgetListModel<Model, meta_model::QMetaListStore::Share> {
    Q_OBJECT
public:
    using base_type = meta_model::QGadgetListModel<Model, meta_model::QMetaListStore::Share>;
    ListModel(QObject* p = nullptr): base_type(p) {}
};

TEST(Store, Basic) {
    meta_model::ShareStore<Model> store;

    ListModel m;
    m.set_store(store);
    m.insert(0, std::array { Model { 1 }, Model { 2 } });

    EXPECT_EQ(m.at(0).uid, 1);
    EXPECT_EQ(m.at(1).uid, 2);
}

TEST(Store, Share) {
    meta_model::ShareStore<Model> store;

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