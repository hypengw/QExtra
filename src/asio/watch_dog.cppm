export module qextra:asio.watch_dog;
export import rstd.cppstd;
export import asio;

export class WatchDog {
public:
    WatchDog() {}
    ~WatchDog() { cancel(); }

    using clock    = asio::steady_timer::clock_type;
    using duration = asio::steady_timer::duration;

    bool is_running() const {
        if (m_timer) {
            return std::chrono::operator>(m_timer->expiry(), clock::now());
        }
        return false;
    }

    template<typename Ex, typename F, typename Allocator = std::allocator<void>>
        requires(! rstd::mtp::same_as<duration, rstd::mtp::rm_cvf<Allocator>>)
    auto watch(Ex&& ex, F&& f, const duration& t = asio::chrono::minutes(5), Allocator&& alloc = {})
        -> asio::awaitable<void> {
        cancel();
        m_timer = std::make_shared<asio::steady_timer>(ex);
        m_timer->expires_after(t);
        return watch_impl<rstd::mtp::decay<F>>(m_timer, rstd::move(f), alloc);
    }

    void cancel() {
        if (m_timer) {
            m_timer->cancel();
            m_timer->expires_after(asio::chrono::seconds(0));
        }
    }

    template<typename Ex, typename F, typename CT, typename Allocator = std::allocator<void>>
    auto spawn(Ex&& ex, F&& f, CT&& ct, const duration& t = asio::chrono::minutes(5),
               Allocator&& alloc = {}) {
        asio::co_spawn(ex, watch(ex, rstd::forward<F>(f), t, alloc), rstd::forward<CT>(ct));
    }

private:
    template<typename F, typename Allocator>
    static auto watch_impl(std::shared_ptr<asio::steady_timer> timer, F f, Allocator alloc)
        -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor_;
        auto [order, exp, ec] =
            co_await asio::experimental::make_parallel_group(
                asio::co_spawn(ex, rstd::move(f), asio::bind_allocator(alloc, asio::deferred_t {})),
                asio::co_spawn(
                    ex, timer->async_wait(asio::use_awaitable_t {}), asio::bind_allocator(alloc, asio::deferred_t {})))
                .async_wait(asio::experimental::wait_for_one(), asio::deferred_t {});

        timer->expires_after(asio::chrono::seconds(0));

        if (exp) std::rethrow_exception(exp);
        co_return;
    }

    std::shared_ptr<asio::steady_timer> m_timer;
};