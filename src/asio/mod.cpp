module;
#include <cerrno>
#include "QExtra/macro_qt.hpp"
module qextra;
using namespace Qt::StringLiterals;

void handle_asio_exception(cppstd::exception_ptr                                   eptr,
                           asio::any_completion_handler<void(cppstd::string_view)> on_error,
                           const cppstd::source_location                           loc) {
    try {
        if (eptr) {
            cppstd::rethrow_exception(eptr);
        }
    } catch (const asio::system_error& ex) {
        auto        code     = ex.code().value();
        const auto& category = ex.code().category();
        bool        warn     = false;
        if (category == asio::error::get_system_category()) {
            if (code == ECANCELED) {
                warn = true;
            }
        }

        if (warn) {
            qWarning() << cppstd::format("[{}] {}", category.name(), ex.what());
        } else {
            qCritical() << cppstd::format("[{}] {}", category.name(), ex.what());
        }
        if (on_error) on_error(ex.what());
    } catch (const cppstd::exception& ex) {
        qCritical() << ex.what();
        if (on_error) on_error(ex.what());
    }
}