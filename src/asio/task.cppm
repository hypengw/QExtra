export module qextra:asio.task;
export import asio;

namespace qextra::prelude
{
export template<typename T, typename Executor = asio::any_io_executor>
using task = asio::awaitable<T, Executor>;

export using asio::as_tuple;

export constexpr asio::use_awaitable_t<> use_task;
export constexpr asio::deferred_t deferred;
} // namespace qcm

