export module qextra:asio;
export import :asio.context;
export import :asio.executor;
export import :asio.watcher;
export import :asio.watch_dog;
export import :asio.task;

export void handle_asio_exception(cppstd::exception_ptr                                   eptr,
                                  asio::any_completion_handler<void(cppstd::string_view)> on_error,
                                  const cppstd::source_location                           loc);