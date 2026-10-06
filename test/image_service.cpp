#include "tests.hpp"
#include <QtCore/QDebug>
#include <rstd/macro.hpp>

import rstd;
import qextra;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace {
template <typename F> bool until(F predicate) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 5000) {
    if (predicate())
      return true;
    QThread::msleep(1);
  }
  return false;
}
} // namespace

int run_image_service(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  for (int mib : {0, 64, 128, 256, 512}) {
    const auto bytes = usize(mib) * usize(1024 * 1024);
    qextra::image::set_cache_limit(bytes);
    rstd_assert(qextra::image::service_statistics().cache_limit == bytes);
    rstd_assert(qextra::image::statistics().limit >=
                bytes + usize(128 * 1024 * 1024));
  }
  qextra::image::set_cache_limit(usize(1024 * 1024 * 1024));
  rstd_assert(qextra::image::service_statistics().cache_limit ==
              usize(512 * 1024 * 1024));
  qextra::image::set_cache_limit(usize(16 * 1024 * 1024));
  auto directory = rstd::fs::TempDir::make("qextra-image-service"_str).unwrap();
  auto file = rstd::path::PathBuf::from(directory.path()).join("image.ppm"_str);
  auto data = Vec<u8>::from("P6\n2 1\n255\n"_str.as_bytes());
  for (auto b : array<u8, 6>{u8(255), u8(), u8(), u8(), u8(255), u8()})
    data.push(b);
  rstd::fs::write(file.as_path(), data.as_slice()).unwrap();
  auto path = file.as_path().to_str().unwrap();
  auto name = QString::fromUtf8(reinterpret_cast<const char *>(path.data()),
                                qsizetype(path.len().to_primitive()));
  {
    auto first = qextra::image::Request::open(name, {}).unwrap();
    auto second = qextra::image::Request::open(name, {}).unwrap();
    rstd_assert(first.advance() && second.advance());
    first.cancel();
    Option<rstd::sync::Arc<qextra::image::Frame>> ready;
    rstd_assert(until([&] {
      auto result = second.poll();
      rstd_assert(result.error.is_none());
      ready = result.frame.take();
      return ready.is_some();
    }));
    rstd_assert(qextra::image::payload_count() == usize(1));
    auto third = qextra::image::Request::open(name, {}).unwrap();
    rstd_assert(third.advance());
    auto shared = third.poll();
    rstd_assert(shared.frame.is_some());
    rstd_assert((*shared.frame)->image.pixels.data() ==
                (*ready)->image.pixels.data());
  }
  rstd_assert(until([] { return qextra::image::payload_count() == usize(); }));
  {
    auto requests = Vec<qextra::image::Request>::make();
    auto ready = Vec<rstd::sync::Arc<qextra::image::Frame>>::make();
    for (int i = 0; i < 80; ++i)
      requests.push(
          qextra::image::Request::open(name, {}, true, false).unwrap());
    for (auto &request : requests)
      rstd_assert(request.advance());
    for (usize i; i < requests.len(); ++i) {
      rstd_assert(until([&] {
        auto result = requests[i].poll();
        rstd_assert(result.error.is_none());
        if (result.frame.is_none())
          return false;
        ready.push(result.frame.unwrap_unchecked());
        return true;
      }));
    }
    rstd_assert(qextra::image::payload_count() == usize(80));
    rstd_assert(until([&] { return requests[usize()].advance(); }));
    rstd_assert(until([&] {
      auto result = requests[usize()].poll();
      rstd_assert(result.error.is_none());
      return result.eof;
    }));
    rstd_assert(qextra::image::payload_count() == usize(80));
  }
  rstd_assert(until([] { return qextra::image::payload_count() == usize(); }));
  {
    auto first = qextra::image::Request::open(name, {}).unwrap();
    auto second = qextra::image::Request::open(name, {}).unwrap();
    first.suspend(true);
    rstd_assert(first.advance());
    rstd_assert(second.advance());
    rstd_assert(until([&] { return second.poll().frame.is_some(); }));
  }
  {
    auto before = qextra::image::service_statistics().decoded;
    auto request = qextra::image::Request::open(name, {}, true, false).unwrap();
    request.suspend(true);
    rstd_assert(request.advance());
    rstd_assert(qextra::image::service_statistics().decoded == before);
    request.cancel();
    rstd_assert(qextra::image::service_statistics().waiting == usize());
  }
  for (int argument = 1; argument < argc; ++argument) {
    qextra::image::clear_cache();
    qextra::image::set_cache_limit(usize(16 * 1024 * 1024));
    auto request = qextra::image::Request::open(
                       QString::fromLocal8Bit(argv[argument]), {}, true)
                       .unwrap();
    auto cycle = [&](bool rewind) {
      usize frames;
      for (;;) {
        rstd_assert(request.advance(rewind));
        rewind = false;
        bool eof = false;
        rstd_assert(until([&] {
          auto result = request.poll();
          rstd_assert(result.error.is_none());
          eof = result.eof;
          if (result.frame) {
            rstd_assert((*result.frame)->image.index ==
                        u64(frames.to_primitive()));
            ++frames;
            return true;
          }
          return eof;
        }));
        if (eof)
          break;
      }
      return frames;
    };
    const auto frames = cycle(false);
    rstd_assert(frames >= usize(3));
    auto cold = qextra::image::service_statistics();
    for (int i = 0; i < 3; ++i)
      rstd_assert(cycle(true) == frames);
    rstd_assert(qextra::image::service_statistics().decoded == cold.decoded);
    rstd_assert(request.advance(true));
    Option<rstd::sync::Arc<qextra::image::Frame>> held;
    rstd_assert(until([&] {
      auto result = request.poll();
      held = result.frame.take();
      return held.is_some();
    }));
    auto other = qextra::image::Request::open(
                     QString::fromLocal8Bit(argv[argument]), {}, true)
                     .unwrap();
    rstd_assert(other.advance(false, 1));
    rstd_assert(until([&] {
      auto result = other.poll();
      if (!result.frame)
        return false;
      rstd_assert((*result.frame)->image.index == u64(1));
      return true;
    }));
    other.cancel();
    qextra::image::set_cache_limit(usize());
    // Eviction removes cache ownership, not a live consumer's frame.
    rstd_assert((*held)->image.index == u64());
    rstd_assert(request.advance());
    rstd_assert(until([&] {
      auto result = request.poll();
      rstd_assert(result.error.is_none());
      if (!result.frame)
        return false;
      rstd_assert((*result.frame)->image.index == u64(1));
      return true;
    }));
    held = None();
    rstd_assert(cycle(true) == frames);
    rstd_assert(qextra::image::service_statistics().decoded > cold.decoded);
    rstd_assert(qextra::image::service_statistics().cache_bytes == usize());
    qextra::image::set_cache_limit(usize(64));
    rstd_assert(cycle(true) == frames);
    rstd_assert(cycle(true) == frames);
    rstd_assert(qextra::image::service_statistics().cache_bytes == usize());
    qInfo() << "animation cache cold/hot/disabled/oversize passed"
            << argv[argument];
  }
  qextra::image::clear_cache();
  rstd_assert(
      until([] { return qextra::image::statistics().live_bytes == usize(); }));
  return 0;
}
