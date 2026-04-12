module;
#include <cerrno>
module qextra;

void handle_asio_exception(cppstd::exception_ptr                                   eptr,
                           asio::any_completion_handler<void(cppstd::string_view)> on_error,
                           const cppstd::source_location                           loc) {
    try {
        if (eptr) {
            cppstd::rethrow_exception(eptr);
        }
    } catch (const asio::system_error& ex) {
        // auto        level    = qcm::LogLevel::ERROR;
        auto        code     = ex.code().value();
        const auto& category = ex.code().category();
        if (category == asio::error::get_system_category()) {
            if (code == ECANCELED) {
                // level = qcm::LogLevel::WARN;
            }
        }

        // qcm::log::log(level, loc, "[{}] {}", category.name(), ex.what());
        if (on_error) on_error(ex.what());
    } catch (const cppstd::exception& ex) {
        // qcm::log::log(qcm::LogLevel::ERROR, loc, "{}", ex.what());
        if (on_error) on_error(ex.what());
    }
}