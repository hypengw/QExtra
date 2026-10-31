#include "QExtra/image.hpp"
#include "tests.hpp"
#include <rstd/macro.hpp>
import rstd;
import qextra;
using namespace rstd::prelude;
namespace {
template <class F> bool until(F predicate) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < 5000) {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QThread::msleep(5);
  }
  return predicate();
}

void test_scroll_return(const QUrl &source) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
  visible: true; width: 128; height: 64; color: "blue"
  Flickable {
    id: viewport
    objectName: "viewport"; anchors.fill: parent; clip: true
    contentWidth: width; contentHeight: 256; interactive: false
    Item {
      objectName: "host"; width: 64; height: 64
      QE.AnimatedImage {
        objectName: "subject"; anchors.fill: parent; smooth: false
        fillMode: QE.Image.Stretch; sharedPlayback: true
        preparationScope: viewport
      }
    }
  }
})",
                    QUrl("qrc:/scroll-return.qml"));
  auto *root = component.create();
  if (!root)
    qCritical() << component.errors();
  rstd_assert(root);
  auto *window = qobject_cast<QQuickWindow *>(root);
  auto *viewport = root->findChild<QQuickItem *>("viewport");
  auto *host = root->findChild<QQuickItem *>("host");
  auto *image = root->findChild<qextra::AnimatedImage *>("subject");
  rstd_assert(window && viewport && host && image);
  rstd_assert(qextra::image::set_texture_cache_limit(window, 0));
  auto stats = [&] { return qextra::image::texture_statistics(window); };
  auto color = [&](bool visible) {
    const auto pixels = window->grabWindow();
    if (pixels.isNull())
      return false;
    const auto sample = pixels.pixelColor(8 * pixels.width() / window->width(),
                                          pixels.height() / 2);
    return visible
               ? sample.red() > 240 && sample.green() < 10 && sample.blue() < 10
               : sample.blue() > 240 && sample.red() < 10;
  };
  image->setSource(source);
  rstd_assert(until(
      [&] { return image->status() == qextra::Image::Ready && color(true); }));
  for (bool moveHost : {false, true}) {
    auto position = [&](bool outside) {
      if (moveHost)
        host->setY(outside ? 96 : 0);
      else
        viewport->setProperty("contentY", outside ? 96 : 0);
      rstd_assert(image->position() == QPointF(0, 0));
      rstd_assert(image->size() == QSizeF(64, 64));
      rstd_assert(image->source() == source);
    };
    position(true);
    image->setLoadingMode(qextra::Image::Prefetch);
    rstd_assert(until([&] { return color(false); }));
    position(false);
    image->setLoadingMode(qextra::Image::Automatic);
    rstd_assert(until([&] {
      return image->status() == qextra::Image::Ready && color(true);
    }));

    position(true);
    image->setLoadingMode(qextra::Image::Prefetch);
    rstd_assert(until([&] { return color(false); }));
    image->setLoadingMode(qextra::Image::Suspended);
    rstd_assert(until([&] {
      window->grabWindow();
      return stats().active_bytes == 0 && stats().idle_bytes == 0 &&
             stats().pending_release_bytes == 0;
    }));
    const auto uploads = stats().uploads;
    image->setLoadingMode(qextra::Image::Prefetch);
    rstd_assert(until([&] { return color(false); }));
    position(false);
    image->setLoadingMode(qextra::Image::Automatic);
    rstd_assert(until([&] {
      return image->status() == qextra::Image::Ready && color(true) &&
             stats().uploads > uploads;
    }));
  }
  delete root;
  qInfo() << "image ancestor scroll, retained return and pooled texture "
             "restore passed";
}
} // namespace
int run_image_texture(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir directory;
  QFile file(directory.filePath("image.ppm"));
  rstd_assert(file.open(QIODevice::WriteOnly));
  file.write(QByteArray("P6\n2 1\n255\n") +
             QByteArray::fromHex("ff000000ff00"));
  file.close();
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
  visible: true; width: 128; height: 64; color: "blue"
  QE.Image { objectName: "a"; width: 64; height: 64; smooth: false; fillMode: QE.Image.Stretch }
  Item {
    width: 64; height: 32; clip: true
    Item {
      objectName: "scope"; width: 64; height: 32; clip: true; visible: false
      QE.Image { objectName: "prepared"; x: 80; width: 32; height: 32; preparationScope: parent; loadingMode: QE.Image.Prefetch; fillMode: QE.Image.Stretch }
    }
  }
  QE.Image { objectName: "b"; x: 64; width: 64; height: 64; smooth: false; fillMode: QE.Image.Stretch; mirror: true }
})",
                    QUrl("qrc:/textures.qml"));
  auto *root = component.create();
  if (!root) {
    qCritical() << component.errors();
    return 1;
  }
  auto *window = qobject_cast<QQuickWindow *>(root);
  auto *a = root->findChild<qextra::Image *>("a");
  auto *b = root->findChild<qextra::Image *>("b");
  rstd_assert(window && a && b);
  const auto source = QUrl::fromLocalFile(file.fileName());
  rstd_assert(qextra::image::set_texture_cache_limit(window, 1024 * 1024));
  using rstd::sync::atomic::Atomic;
  using rstd::sync::atomic::Ordering;
  Atomic<usize> synchronized{usize()}, committed{usize()};
  const auto syncConnection = QObject::connect(
      window, &QQuickWindow::afterSynchronizing, window,
      [&] {
        if (synchronized.load(Ordering::Acquire) != usize())
          return;
        for (auto *item : {a, b}) {
          auto *provider = item->textureProvider();
          auto *texture = provider ? provider->texture() : nullptr;
          if (!texture || texture->textureSize() != QSize(2, 1))
            continue;
          rstd_assert(!texture->rhiTexture());
          synchronized.store(usize(1), Ordering::Release);
          break;
        }
      },
      Qt::DirectConnection);
  const auto renderConnection = QObject::connect(
      window, &QQuickWindow::afterRendering, window,
      [&] {
        if (synchronized.load(Ordering::Acquire) == usize() ||
            committed.load(Ordering::Acquire) != usize())
          return;
        for (auto *item : {a, b}) {
          auto *provider = item->textureProvider();
          auto *texture = provider ? provider->texture() : nullptr;
          if (texture && texture->textureSize() == QSize(2, 1))
            rstd_assert(texture->rhiTexture());
        }
        committed.store(usize(1), Ordering::Release);
      },
      Qt::DirectConnection);
  a->setSource(source);
  b->setSource(source);
  auto stats = [&] { return qextra::image::texture_statistics(window); };
  auto colors = [&](QQuickWindow *w, bool both) {
    auto pixels = w->grabWindow();
    if (pixels.isNull())
      return false;
    auto sample = [&](int x) {
      return pixels.pixelColor(x * pixels.width() / w->width(),
                               pixels.height() / 2);
    };
    return sample(8).red() > 240 && sample(8).green() < 10 &&
           sample(8).blue() < 10 &&
           (!both || (sample(72).green() > 240 && sample(72).red() < 10));
  };
  rstd_assert(until([&] {
    return a->status() == qextra::Image::Ready &&
           b->status() == qextra::Image::Ready && colors(window, true);
  }));
  rstd_assert(synchronized.load(Ordering::Acquire) == usize(1) &&
              committed.load(Ordering::Acquire) == usize(1));
  QObject::disconnect(syncConnection);
  QObject::disconnect(renderConnection);
  rstd_assert(stats().allocations == 1 && stats().uploads == 1 &&
              stats().hits >= 1 && stats().active_bytes == 8);
  a->setSource({});
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().active_bytes == 8;
  }));
  b->setSource({});
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().idle_bytes == 8 && stats().active_bytes == 0;
  }));
  a->setSource(source);
  rstd_assert(until(
      [&] { return colors(window, false) && stats().active_bytes == 8; }));
  rstd_assert(stats().allocations == 1 && stats().uploads == 1);
  b->setMipmap(true);
  b->setSource(source);
  rstd_assert(
      until([&] { return colors(window, true) && stats().uploads == 2; }));
  rstd_assert(stats().allocations == 2 && stats().active_bytes == 20);
  b->setMipmap(false);
  rstd_assert(
      until([&] { return colors(window, true) && stats().active_bytes == 8; }));
  rstd_assert(stats().allocations == 2 && stats().uploads == 2);
  a->setFillMode(qextra::Image::Tile);
  rstd_assert(until([&] {
    const auto pixels = window->grabWindow();
    if (pixels.isNull())
      return false;
    int transitions{};
    bool red{};
    for (int x = 1; x < pixels.width() / 2 - 1; ++x) {
      const bool next = pixels.pixelColor(x, pixels.height() / 2).red() > 128;
      if (next != red)
        ++transitions;
      red = next;
    }
    return transitions > 8 &&
           pixels.pixelColor(pixels.width() * 3 / 4 - 8, pixels.height() / 2)
                   .green() > 240;
  }));
  rstd_assert(qextra::image::set_texture_cache_limit(window, 0));
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().idle_bytes == 0 && stats().active_bytes == 8;
  }));
  a->setSource({});
  b->setSource({});
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().active_bytes == 0 && stats().pending_release_bytes == 0;
  }));
  qextra::image::set_texture_cache_limit(window, 1024 * 1024);
  a->setCache(false);
  a->setSource(source);
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().active_bytes == 8;
  }));
  a->setSource({});
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().active_bytes == 0 && stats().idle_bytes == 0 &&
           stats().pending_release_bytes == 0;
  }));
  a->setCache(true);
  a->setFillMode(qextra::Image::Stretch);
  a->setSource(source);
  rstd_assert(until([&] { return colors(window, false); }));
  {
    QQuickWindow other;
    other.setGeometry(0, 0, 64, 64);
    other.show();
    rstd_assert(until([&] { return other.isExposed(); }));
    a->setParentItem(other.contentItem());
    rstd_assert(a->window() == &other);
    rstd_assert(until([&] { return colors(&other, false); }));
    const auto otherStats = qextra::image::texture_statistics(&other);
    qInfo() << "other window" << otherStats.allocations << otherStats.uploads
            << otherStats.hits << otherStats.generation;
    rstd_assert(otherStats.allocations == 1);
    const auto generation =
        qextra::image::texture_statistics(&other).generation;
    other.setPersistentGraphics(false);
    other.setPersistentSceneGraph(false);
    other.hide();
    other.releaseResources();
    rstd_assert(until([&] { return !other.isExposed(); }));
    other.show();
    rstd_assert(until([&] { return colors(&other, false); }));
    const auto restored = qextra::image::texture_statistics(&other);
    // Window hiding is permitted to preserve the scene graph.
    rstd_assert(restored.allocations ==
                (restored.generation > generation ? 2 : 1));
    qInfo() << "context restored" << restored.generation
            << restored.allocations;
    a->setParentItem(window->contentItem());
    rstd_assert(until([&] { return colors(window, false); }));
  }
  a->setSource({});
  qextra::image::trim_texture_cache(window);
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().idle_bytes == 0 && stats().active_bytes == 0;
  }));
  rstd_assert(QFile::copy(file.fileName(), directory.filePath("second.ppm")));
  const auto secondSource =
      QUrl::fromLocalFile(directory.filePath("second.ppm"));
  qextra::image::set_texture_cache_limit(window, 8);
  const auto beforeLru = stats().allocations;
  a->setSource(source);
  rstd_assert(until([&] { return colors(window, false); }));
  a->setSource({});
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().idle_bytes == 8;
  }));
  a->setSource(secondSource);
  rstd_assert(until([&] {
    return a->status() == qextra::Image::Ready && colors(window, false) &&
           stats().allocations == beforeLru + 2;
  }));
  a->setSource({});
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().active_bytes == 0 && stats().idle_bytes == 8;
  }));
  a->setSource(source);
  rstd_assert(until([&] {
    return a->status() == qextra::Image::Ready && colors(window, false) &&
           stats().allocations == beforeLru + 3;
  }));
  a->setSource({});
  qextra::image::trim_texture_cache(window);
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().active_bytes == 0 && stats().idle_bytes == 0;
  }));
  auto *scope = root->findChild<QQuickItem *>("scope");
  auto *prepared = root->findChild<qextra::Image *>("prepared");
  rstd_assert(QFile::copy(file.fileName(), directory.filePath("prepare.ppm")));
  const auto preparedSource =
      QUrl::fromLocalFile(directory.filePath("prepare.ppm"));
  scope->setVisible(true);
  const auto beforePrepare = stats();
  prepared->setSource(preparedSource);
  rstd_assert(
      until([&] { return prepared->status() == qextra::Image::Ready; }));
  window->grabWindow();
  rstd_assert(stats().uploads == beforePrepare.uploads);
  const auto preparedDecoded = qextra::image::service_statistics().decoded;
  prepared->setX(0);
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().uploads > beforePrepare.uploads;
  }));
  rstd_assert(qextra::image::service_statistics().decoded == preparedDecoded);
  prepared->setLoadingMode(qextra::Image::Suspended);
  rstd_assert(until([&] {
    window->grabWindow();
    return stats().active_bytes == 0;
  }));
  prepared->setLoadingMode(qextra::Image::Prefetch);
  prepared->setX(80);
  rstd_assert(
      until([&] { return prepared->status() == qextra::Image::Ready; }));
  prepared->setSource({});
  scope->setY(40);
  rstd_assert(QFile::copy(file.fileName(), directory.filePath("outside.ppm")));
  prepared->setSource(QUrl::fromLocalFile(directory.filePath("outside.ppm")));
  const auto outsideDecoded = qextra::image::service_statistics().decoded;
  for (int i = 0; i < 8; ++i)
    QCoreApplication::processEvents();
  rstd_assert(prepared->status() == qextra::Image::Loading);
  rstd_assert(qextra::image::service_statistics().decoded == outsideDecoded);
  scope->setY(0);
  rstd_assert(
      until([&] { return prepared->status() == qextra::Image::Ready; }));
  delete root;
  test_scroll_return(source);
  qextra::image::clear_cache();
  rstd_assert(
      until([] { return qextra::image::statistics().live_bytes == usize(); }));
  qInfo() << "texture sharing, sampling, residency, budgets and context "
             "lifetime passed";
  return 0;
}
