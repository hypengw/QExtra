#pragma once

#include <span>
#include "meta_model/item_trait.hpp"

namespace meta_model
{

///
/// @brief store_type
template<typename T, typename TItem>
concept storeable =
    requires(T t, TItem item, typename ItemTrait<TItem>::key_type key, std::int64_t handle) {
        { t.store_query(key) } -> std::same_as<TItem*>;
        t.store_insert(item, true, handle);
        t.store_remove(key);
        {
            t.store_reg_notify([](decltype(key)) {
            })
        } -> std::same_as<std::int64_t>;
        t.store_unreg_notify(handle);
    };

///
/// @brief Item that defined store_type in ItemTrait
template<typename T>
concept storeable_item = hashable_item<T> && requires(T t) { typename ItemTrait<T>::store_type; } &&
                         storeable<typename ItemTrait<T>::store_type, T>;
} // namespace meta_model