#include "QExtra/image.hpp"
#include "tests.hpp"
#include <rstd/macro.hpp>

import rstd;
import qextra;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace {
template <typename F> bool wait_for(F predicate, int timeout = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents();
    QThread::msleep(5);
  }
  return predicate();
}
} // namespace

int run_image(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  auto directory = rstd::fs::TempDir::make("qextra-image"_str).unwrap();
  auto path = rstd::path::PathBuf::from(directory.path()).join("image.ppm"_str);
  auto data = Vec<u8>::from("P6\n2 1\n255\n"_str.as_bytes());
  for (auto b : array<u8, 6>{u8(255), u8(), u8(), u8(), u8(255), u8()})
    data.push(b);
  rstd::fs::write(path.as_path(), data.as_slice()).unwrap();
  auto pathString = path.as_path().to_str().unwrap();
  auto source = QUrl::fromLocalFile(
      QString::fromUtf8(reinterpret_cast<const char *>(pathString.data()),
                        qsizetype(pathString.len().to_primitive())));
  {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
    visible: true; width: 128; height: 64; color: "blue"
    QE.Image { objectName: "still"; width: 64; height: 64; maxSize: Qt.size(512, 512); smooth: false; layer.enabled: true }
    QE.AnimatedImage { objectName: "animation"; x: 64; width: 64; height: 64; smooth: false }
})",
                      QUrl("qrc:/test.qml"));
    auto *root = component.create();
    if (!root) {
      qCritical() << component.errors();
      return 1;
    }
    auto *window = qobject_cast<QQuickWindow *>(root);
    auto *still = root->findChild<qextra::Image *>("still");
    auto *animation = root->findChild<qextra::AnimatedImage *>("animation");
    rstd_assert(window && still && animation);
    still->setSource(source);
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    rstd_assert(still->implicitWidth() == 2 && still->implicitHeight() == 1);
    rstd_assert(still->sourceSize() == QSize(2, 1));
    rstd_assert(still->maxSize() == QSize(512, 512));
    rstd_assert(wait_for([&] {
      auto capture = window->grabWindow();
      if (capture.isNull())
        return false;
      auto scale = qreal(capture.width()) / window->width();
      return capture.pixelColor(int(8 * scale), int(32 * scale)).red() > 240 &&
             capture.pixelColor(int(56 * scale), int(32 * scale)).green() > 240;
    }));
    rstd_assert(
        wait_for([] { return qextra::image::payload_count() == usize(); }));
    still->setMaxSize(QSize(1, 1));
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    rstd_assert(still->sourceSize() == QSize(1, 1));
    still->setMaxSize(QSize(512, 512));
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    rstd_assert(still->sourceSize() == QSize(2, 1));
    still->setSource(QUrl::fromLocalFile("/nonexistent-qextra-image"));
    still->setSource(source);
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    if (argc > 1) {
      animation->setSource(
          QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
      rstd_assert(wait_for([&] { return animation->currentFrame() > 0; }));
      animation->setPaused(true);
      auto frame = animation->currentFrame();
      wait_for([] { return false; }, 250);
      rstd_assert(animation->currentFrame() == frame);
      animation->setPaused(false);
      rstd_assert(wait_for([&] { return animation->currentFrame() != frame; }));
      animation->setCurrentFrame(1);
      rstd_assert(wait_for([&] {
        return animation->status() == qextra::Image::Ready &&
               animation->currentFrame() == 1;
      }));
      animation->setLoops(1);
      animation->restart();
      rstd_assert(wait_for([&] { return animation->currentFrame() == 0; }));
      QElapsedTimer playback;
      playback.start();
      rstd_assert(wait_for([&] { return !animation->playing(); }));
      rstd_assert(playback.elapsed() >= 500);
      rstd_assert(animation->frameCount() >= 3);
      animation->setSource(QUrl());
    }
    window->setPersistentGraphics(false);
    window->setPersistentSceneGraph(false);
    window->hide();
    window->releaseResources();
    wait_for([] { return false; }, 100);
    window->show();
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    still->setSource(QUrl());
    rstd_assert(still->status() == qextra::Image::Null);
    delete root;
  }
  rstd_assert(
      wait_for([] { return qextra::image::payload_count() == usize(); }));
  qextra::image::clear_cache();
  rstd_assert(wait_for(
      [] { return qextra::image::statistics().live_bytes == usize(); }));
  for (int workers : {2, 4}) {
    rstd_assert(qextra::image::set_worker_limit(usize(workers)));
    for (int count : {12, 36, 81}) {
      qextra::image::set_cache_limit(usize());
      QQmlEngine engine;
      QQmlComponent component(&engine);
      component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
    id: scene
    width: 360; height: 40; visible: true
    property int count: 0
    property url stillSource
    property url animationSource
    Item {
        objectName: "viewport"; anchors.fill: parent; clip: true
        Repeater {
            model: scene.count
            QE.AnimatedImage {
                required property int index
                objectName: "tile"
                x: index % 9 * 40; y: Math.floor(index / 9) * 40
                width: 40; height: 40; layer.enabled: true
                maxSize: Qt.size(512, 512)
                fillMode: QE.Image.PreserveAspectCrop
                source: index % 2 ? scene.animationSource : scene.stillSource
            }
        }
    }
})",
                        QUrl("qrc:/resize.qml"));
      auto *root = component.create();
      if (!root) {
        qCritical() << component.errors();
        return 1;
      }
      auto *window = qobject_cast<QQuickWindow *>(root);
      rstd_assert(wait_for([&] { return window->isExposed(); }));
      root->setProperty("stillSource", source);
      root->setProperty(
          "animationSource",
          argc > 1 ? QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1]))
                   : source);
      root->setProperty("count", count);
      auto *viewport = root->findChild<QQuickItem *>("viewport");
      QList<qextra::AnimatedImage *> images;
      rstd_assert(wait_for([&] {
        images.clear();
        for (auto *child : viewport->childItems())
          if (auto *image = qobject_cast<qextra::AnimatedImage *>(child))
            images.push_back(image);
        return images.size() == count;
      }));
      rstd_assert(images.size() == count);
      rstd_assert(wait_for([&] {
        for (auto *item : images)
          if (item->y() < 40 && item->status() != qextra::Image::Ready)
            return false;
        return true;
      }));
      for (auto *item : images)
        if (item->y() >= 40)
          rstd_assert(item->status() == qextra::Image::Loading);
      QElapsedTimer elapsed;
      elapsed.start();
      window->resize(360, 360);
      const bool loaded = wait_for([&] {
        for (auto *item : images) {
          rstd_assert(item->status() != qextra::Image::Error);
          if (item->status() != qextra::Image::Ready)
            return false;
        }
        return true;
      });
      if (!loaded) {
        qWarning() << "resize stalled" << window->size() << viewport->size();
        for (auto *item : images)
          qWarning() << item->x() << item->y() << item->size()
                     << item->isVisible() << item->status();
        const auto stats = qextra::image::service_statistics();
        qWarning() << "running/waiting" << stats.running.to_primitive()
                   << stats.waiting.to_primitive();
      }
      rstd_assert(loaded);
      for (auto *item : images) {
        rstd_assert(item->sourceSize().width() > 0 &&
                    item->sourceSize().width() <= 512);
        rstd_assert(item->sourceSize().height() > 0 &&
                    item->sourceSize().height() <= 512);
      }
      qInfo() << "resize without scrolling" << count << "items" << workers
              << "workers" << elapsed.elapsed() << "ms";
      viewport->setVisible(false);
      rstd_assert(wait_for([] {
        return qextra::image::service_statistics().running == usize();
      }));
      QCoreApplication::processEvents();
      auto decoded = qextra::image::service_statistics().decoded;
      wait_for([] { return false; }, 250);
      rstd_assert(qextra::image::service_statistics().decoded == decoded);
      viewport->setVisible(true);
      if (argc > 1)
        rstd_assert(wait_for([&] {
          return qextra::image::service_statistics().decoded > decoded;
        }));
      delete root;
      rstd_assert(
          wait_for([] { return qextra::image::payload_count() == usize(); }));
      qextra::image::clear_cache();
      rstd_assert(wait_for(
          [] { return qextra::image::statistics().live_bytes == usize(); }));
    }
  }
  return 0;
}
