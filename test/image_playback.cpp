#include "QExtra/image.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QPluginLoader>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickWindow>
#include <rstd/macro.hpp>
import rstd;
import qextra.image.service;
import qextra.image.playback;

Q_IMPORT_PLUGIN(QExtraPlugin)
using namespace rstd::prelude;
namespace qi = qextra::image;

template <typename F> bool wait_for(F predicate, int timeout = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QThread::msleep(2);
  }
  return predicate();
}
void drain() {
  qi::clear_cache();
  rstd_assert(wait_for([] { return qi::statistics().live_bytes == usize(); }));
}
qi::Playback join(QQmlEngine &engine, const QString &path, bool active = true,
                  wavsen::image::DecodeRequest size = {}, bool cache = true) {
  return qi::Playback::open(&engine,
                            qi::Request::open(path, size, true, cache).unwrap(),
                            nullptr, active)
      .unwrap();
}
void service_tests(const QString &path) {
  usize baselineDecoded, baselineOpened;
  for (int count : {1, 2, 12, 81}) {
    drain();
    qi::set_cache_limit(usize());
    const auto before = qi::service_statistics();
    QElapsedTimer latency;
    latency.start();
    {
      QQmlEngine engine;
      auto subscriptions = Vec<qi::Playback>::make();
      for (int i = 0; i < count; ++i)
        subscriptions.push(join(engine, path));
      rstd_assert(wait_for([&] {
        return subscriptions[usize()].snapshot(false).frame.is_some();
      }));
      const auto firstMs = latency.elapsed();
      rstd_assert(qi::service_statistics().opened == before.opened + usize(1));
      rstd_assert(qi::playback_statistics(&engine).sessions == usize(1));
      rstd_assert(qi::playback_statistics(&engine).active == usize(count));
      rstd_assert(wait_for([&] {
        auto first = subscriptions[usize()].snapshot(false);
        rstd_assert(first.error.is_none());
        for (auto &subscription : subscriptions) {
          auto value = subscription.snapshot(false);
          rstd_assert(value.version == first.version && value.frame.is_some());
          rstd_assert((*value.frame)->image.pixels.data() ==
                      (*first.frame)->image.pixels.data());
        }
        rstd_assert(qi::service_statistics().running <= usize(2));
        return first.version >= 6;
      }));
      for (auto &subscription : subscriptions)
        subscription.suspend(true);
      rstd_assert(
          wait_for([] { return qi::service_statistics().running == usize(); }));
      auto stats = qi::service_statistics();
      if (count == 1) {
        baselineDecoded = stats.decoded - before.decoded;
        baselineOpened = stats.opened - before.opened;
      } else {
        rstd_assert(stats.decoded - before.decoded == baselineDecoded);
        rstd_assert(stats.opened - before.opened == baselineOpened);
      }
      const auto pumps = qi::playback_statistics(&engine).pumps;
      wait_for([] { return false; }, 150);
      rstd_assert(qi::service_statistics().decoded == stats.decoded);
      rstd_assert(qi::playback_statistics(&engine).pumps <= pumps + 2);
      const auto version = subscriptions[usize()].snapshot(false).version;
      subscriptions[usize()].suspend(false);
      rstd_assert(wait_for([&] {
        return subscriptions[usize()].snapshot(false).version > version;
      }));
      QElapsedTimer joined;
      joined.start();
      auto late = join(engine, path);
      auto current = subscriptions[usize()].snapshot(false);
      auto lateFrame = late.snapshot(false);
      rstd_assert(lateFrame.version == current.version);
      rstd_assert((*lateFrame.frame)->image.pixels.data() ==
                  (*current.frame)->image.pixels.data());
      qInfo() << path << "subscribers" << count << "first ms" << firstMs
              << "join ms" << joined.elapsed() << "decoded"
              << baselineDecoded.to_primitive() << "opened"
              << baselineOpened.to_primitive() << "live bytes"
              << qi::statistics().live_bytes.to_primitive();
      if (count == 81) {
        auto distinct = join(engine, path, true, {u32(1), u32(1)});
        rstd_assert(
            wait_for([&] { return distinct.snapshot(false).frame.is_some(); }));
      }
    }
    drain();
  }
  for (int bytes : {0, 64, 64 * 1024 * 1024, 512 * 1024 * 1024}) {
    qi::set_cache_limit(usize(bytes));
    {
      QQmlEngine engine;
      auto first = join(engine, path);
      auto second = join(engine, path);
      rstd_assert(wait_for([&] { return first.snapshot(false).version >= 4; }));
      const auto version = first.snapshot(false).version;
      qi::clear_cache();
      rstd_assert(
          wait_for([&] { return first.snapshot(false).version > version; }));
      rstd_assert(first.snapshot(false).version ==
                  second.snapshot(false).version);
      auto differentSize = join(engine, path, true, {u32(1), u32(1)});
      auto differentCache = join(engine, path, true, {}, false);
      rstd_assert(qi::playback_statistics(&engine).sessions == usize(3));
      QQmlEngine other;
      auto differentEngine = join(other, path);
      rstd_assert(qi::playback_statistics(&other).sessions == usize(1));
    }
    drain();
  }
  {
    auto *engine = new QQmlEngine;
    auto first = join(*engine, path);
    auto second = join(*engine, path);
    rstd_assert(
        wait_for([&] { return first.snapshot(false).frame.is_some(); }));
    delete engine;
    rstd_assert(first.snapshot().error.is_some());
    rstd_assert(second.snapshot().error.is_some());
  }
  drain();
}

void ui_tests(const QString &path) {
  qi::set_cache_limit(usize());
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
  visible: true; width: 192; height: 64
  QE.AnimatedImage { objectName: "first"; width: 64; height: 64; sharedPlayback: true; smooth: false }
  QE.AnimatedImage { objectName: "second"; x: 64; width: 64; height: 64; sharedPlayback: true; smooth: false }
  QE.AnimatedImage { objectName: "third"; x: 128; width: 64; height: 64; sharedPlayback: true; smooth: false }
})",
                    QUrl("qrc:/shared.qml"));
  auto *root = component.create();
  if (!root)
    qCritical() << component.errors();
  rstd_assert(root);
  auto *window = qobject_cast<QQuickWindow *>(root);
  auto *a = root->findChild<qextra::AnimatedImage *>("first");
  auto *b = root->findChild<qextra::AnimatedImage *>("second");
  auto *c = root->findChild<qextra::AnimatedImage *>("third");
  const auto source = QUrl::fromLocalFile(path);
  rstd_assert(wait_for([&] { return window->isExposed(); }));
  const auto before = qi::service_statistics().opened;
  a->setSource(source);
  b->setSource(source);
  c->setSource(source);
  rstd_assert(wait_for([&] {
    return a->status() == qextra::Image::Ready &&
           b->status() == qextra::Image::Ready &&
           c->status() == qextra::Image::Ready;
  }));
  rstd_assert(qi::service_statistics().opened == before + usize(1));
  rstd_assert(qi::playback_statistics(&engine).sessions == usize(1));
  rstd_assert(wait_for([&] {
    const auto pixels = window->grabWindow();
    if (pixels.isNull())
      return false;
    const int width = pixels.width() / 3;
    return pixels.copy(0, 0, width, pixels.height()) ==
               pixels.copy(width, 0, width, pixels.height()) &&
           pixels.copy(0, 0, width, pixels.height()) ==
               pixels.copy(width * 2, 0, width, pixels.height());
  }));
  rstd_assert(wait_for([&] {
    return a->currentFrame() > 0 && a->currentFrame() == b->currentFrame() &&
           b->currentFrame() == c->currentFrame();
  }));
  {
    QQuickWindow other;
    other.resize(64, 64);
    other.show();
    rstd_assert(wait_for([&] { return other.isExposed(); }));
    c->setX(0);
    c->setParentItem(other.contentItem());
    rstd_assert(wait_for([&] {
      return qi::playback_statistics(&engine).active == usize(3) &&
             c->currentFrame() == b->currentFrame();
    }));
    rstd_assert(qi::playback_statistics(&engine).sessions == usize(1));
    c->setParentItem(nullptr);
    rstd_assert(wait_for(
        [&] { return qi::playback_statistics(&engine).active == usize(2); }));
    c->setParentItem(window->contentItem());
    c->setX(128);
  }
  a->setPaused(true);
  rstd_assert(wait_for([&] { return a->status() == qextra::Image::Ready; }));
  const int paused = a->currentFrame();
  rstd_assert(wait_for([&] { return b->currentFrame() != paused; }));
  rstd_assert(a->currentFrame() == paused);
  rstd_assert(qi::playback_statistics(&engine).active == usize(2));
  a->setPaused(false);
  rstd_assert(qi::playback_statistics(&engine).active == usize(2));
  a->setSharedPlayback(false);
  a->setSharedPlayback(true);
  rstd_assert(wait_for(
      [&] { return qi::playback_statistics(&engine).active == usize(3); }));
  a->setSpeed(2);
  rstd_assert(qi::playback_statistics(&engine).active == usize(2));
  a->setSpeed(1);
  a->setSharedPlayback(false);
  a->setSharedPlayback(true);
  a->setCurrentFrame(2);
  rstd_assert(qi::playback_statistics(&engine).active == usize(2));
  rstd_assert(wait_for([&] {
    return a->status() == qextra::Image::Ready && a->currentFrame() == 2;
  }));
  a->setSharedPlayback(false);
  a->setSharedPlayback(true);
  a->setLoops(1);
  rstd_assert(qi::playback_statistics(&engine).active == usize(2));
  rstd_assert(wait_for([&] { return !a->playing(); }));
  a->setLoops(-1);
  a->setPlaying(true);
  a->setSharedPlayback(false);
  a->setSharedPlayback(true);
  a->setPlaying(false);
  rstd_assert(qi::playback_statistics(&engine).active == usize(2));
  rstd_assert(wait_for([&] { return a->status() == qextra::Image::Ready; }));
  a->setSource(QUrl());
  b->setVisible(false);
  c->setVisible(false);
  rstd_assert(wait_for(
      [&] { return qi::playback_statistics(&engine).active == usize(); }));
  rstd_assert(
      wait_for([] { return qi::service_statistics().running == usize(); }));
  const auto decoded = qi::service_statistics().decoded;
  wait_for([] { return false; }, 200);
  rstd_assert(qi::service_statistics().decoded == decoded);
  b->setVisible(true);
  c->setVisible(true);
  rstd_assert(wait_for([&] {
    return qi::playback_statistics(&engine).active == usize(2) &&
           b->currentFrame() == c->currentFrame();
  }));
  window->setPersistentGraphics(false);
  window->setPersistentSceneGraph(false);
  window->hide();
  window->releaseResources();
  wait_for([] { return false; }, 100);
  const auto sessions = qi::playback_statistics(&engine).sessions;
  window->show();
  rstd_assert(wait_for([&] {
    return window->isExposed() &&
           qi::playback_statistics(&engine).active == usize(2);
  }));
  rstd_assert(qi::playback_statistics(&engine).sessions == sessions);
  c->restart();
  rstd_assert(qi::playback_statistics(&engine).active == usize(1));
  rstd_assert(wait_for([&] {
    return c->status() == qextra::Image::Ready && c->currentFrame() == 0;
  }));
  c->setSource(QUrl());
  QPointer<qextra::AnimatedImage> doomed(b);
  QObject::connect(b, &qextra::Image::currentFrameChanged, b,
                   [b] { delete b; });
  rstd_assert(wait_for([&] { return doomed.isNull(); }));
  delete root;
  drain();
}

void initialization_tests(const QString &path) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
  id: scene
  property url imageSource
  visible: true; width: 128; height: 64
  QE.AnimatedImage {
    objectName: "first"; width: 64; height: 64
    source: scene.imageSource; currentFrame: 1; sharedPlayback: true; playing: false
  }
  QE.AnimatedImage {
    objectName: "second"; x: 64; width: 64; height: 64
    sharedPlayback: true; paused: true; currentFrame: 1; source: scene.imageSource
  }
})",
                    QUrl("qrc:/initial-shared.qml"));
  auto *root = component.createWithInitialProperties(
      {{QStringLiteral("imageSource"), QUrl::fromLocalFile(path)}});
  if (!root)
    qCritical() << component.errors();
  rstd_assert(root);
  auto *first = root->findChild<qextra::AnimatedImage *>("first");
  auto *second = root->findChild<qextra::AnimatedImage *>("second");
  rstd_assert(wait_for([&] {
    return first->status() == qextra::Image::Ready &&
           second->status() == qextra::Image::Ready;
  }));
  rstd_assert(first->currentFrame() == 1 && second->currentFrame() == 1);
  rstd_assert(qi::playback_statistics(&engine).sessions == usize());
  delete root;
  drain();
}

void static_tests() {
  QTemporaryDir directory;
  rstd_assert(directory.isValid());
  const auto path = directory.filePath("still.ppm");
  auto write = [&](const QByteArray &value) {
    QFile file(path);
    rstd_assert(file.open(QIODevice::WriteOnly));
    rstd_assert(file.write(value) == value.size());
  };
  write(QByteArray("P6\n1 1\n255\n") + QByteArray::fromHex("ff0000"));
  qi::set_cache_limit(usize());
  {
    QQmlEngine engine;
    auto first = join(engine, path);
    auto second = join(engine, path, true,
                       {u32(), u32(), wavsen::image::ResizeMode::Cover});
    const auto before = qi::service_statistics().decoded;
    rstd_assert(
        wait_for([&] { return first.snapshot(false).frame.is_some(); }));
    auto snapshot = first.snapshot(false);
    rstd_assert((*snapshot.frame)->image.pixels.data() ==
                (*second.snapshot(false).frame)->image.pixels.data());
    rstd_assert(qi::service_statistics().decoded == before + usize(1));
    const auto pumps = qi::playback_statistics(&engine).pumps;
    wait_for([] { return false; }, 100);
    rstd_assert(qi::service_statistics().decoded == before + usize(1));
    rstd_assert(qi::playback_statistics(&engine).pumps <= pumps + 1);
    auto late = join(engine, path);
    rstd_assert(late.snapshot(false).version == snapshot.version);
    write(QByteArray("P6\n2 1\n255\n") + QByteArray::fromHex("0000ff0000ff"));
    auto changed = join(engine, path);
    rstd_assert(
        wait_for([&] { return changed.snapshot(false).frame.is_some(); }));
    rstd_assert((*changed.snapshot(false).frame)->image.width == u32(2));
    rstd_assert(qi::playback_statistics(&engine).sessions == usize(2));
    auto invalid = join(engine, directory.filePath("missing"));
    auto alsoInvalid = join(engine, directory.filePath("missing"));
    rstd_assert(wait_for([&] { return invalid.snapshot().error.is_some(); }));
    rstd_assert(alsoInvalid.snapshot().error.is_some());
    rstd_assert(invalid.snapshot().error.is_some());
  }
  drain();
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  static_tests();
  for (int i = 1; i < argc; ++i) {
    const auto path = QString::fromLocal8Bit(argv[i]);
    initialization_tests(path);
    service_tests(path);
    ui_tests(path);
  }
  qInfo()
      << "shared playback lifecycle, independent controls and pixels passed";
}
