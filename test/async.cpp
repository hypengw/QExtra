#include <gtest/gtest.h>

import qextra;

TEST(Async, SpawnTransfersJoinHandleOwnership) {
    auto  argc = 1;
    char  name[] { "qextra_test" };
    char* argv[] { name, nullptr };
    auto  app = QCoreApplication { argc, argv };

    QAsyncResult::initEx(&app, 1, nullptr);
    {
        auto ran    = std::atomic_bool { false };
        auto result = QAsyncResult {};
        result.spawn([&ran]() -> qextra::prelude::task<void> {
            ran.store(true);
            co_return;
        });

        QTimer::singleShot(std::chrono::milliseconds(100), &app, &QCoreApplication::quit);
        app.exec();

        EXPECT_TRUE(ran.load());
    }
    QAsyncResult::dropEx();
}
