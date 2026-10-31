#include "QExtra/image.hpp"
#include "tests.hpp"
#include <rstd/macro.hpp>

import rstd;
import qextra;

using namespace rstd::prelude;

namespace {
template <typename F> bool wait_for(F predicate) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < 5000) {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QThread::msleep(2);
  }
  return predicate();
}

QUrl write(const QTemporaryDir &directory, const QString &name,
           const QByteArray &bytes) {
  const auto path = directory.filePath(name);
  QFile file(path);
  rstd_assert(file.open(QIODevice::WriteOnly));
  rstd_assert(file.write(bytes) == bytes.size());
  return QUrl::fromLocalFile(path);
}
} // namespace

int run_image_metadata(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir directory;
  rstd_assert(directory.isValid());
  const auto wide =
      write(directory, QStringLiteral("wide.ppm"),
            QByteArray("P6\n4 2\n255\n") + QByteArray(4 * 2 * 3, '\x7f'));
  const auto tall =
      write(directory, QStringLiteral("tall.ppm"),
            QByteArray("P6\n2 6\n255\n") + QByteArray(2 * 6 * 3, '\x3f'));
  const auto animation = write(
      directory, QStringLiteral("animation.gif"),
      QByteArray::fromHex("47494638396102000100800000ff000000ff00"
                          "21ff0b4e45545343415045322e300301000000"
                          "21f904000a0000002c0000000002000100000202040a00"
                          "21f904000a0000002c00000000020001000002024c0a003b"));
  QImage jpeg(4, 2, QImage::Format_RGB32);
  jpeg.fill(Qt::red);
  const auto jpegPath = directory.filePath(QStringLiteral("plain.jpg"));
  rstd_assert(jpeg.save(jpegPath, "JPEG"));
  QFile jpegFile(jpegPath);
  rstd_assert(jpegFile.open(QIODevice::ReadOnly));
  const auto jpegBytes = jpegFile.readAll();
  const auto exif = QByteArray::fromHex(
      "45786966000049492a0008000000010012010300010000000600000000000000");
  const auto segment =
      QByteArray::fromHex("ffe1") + QByteArray::fromHex("0022") + exif;
  const auto oriented = write(directory, QStringLiteral("oriented.jpg"),
                              jpegBytes.left(2) + segment + jpegBytes.mid(2));
  qextra::image::set_cache_limit(usize(16 * 1024 * 1024));
  {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QExtra as QE
Window {
    width: 256; height: 128; visible: true
    QE.Image {
        objectName: "still"; width: 64; height: 64
        sourceSize: Qt.size(2, 2)
        retainWhileLoading: true
    }
    QE.AnimatedImage {
        objectName: "first"; x: 64; width: 64; height: 64
        sourceSize: Qt.size(1, 1); sharedPlayback: true
    }
    QE.AnimatedImage {
        objectName: "second"; x: 128; width: 64; height: 64
        sourceSize: Qt.size(1, 1); sharedPlayback: true
    }
})",
                      QUrl("qrc:/metadata.qml"));
    auto *root = component.create();
    if (!root) {
      qCritical() << component.errors();
      return 1;
    }
    auto *still = root->findChild<qextra::Image *>("still");
    auto *first = root->findChild<qextra::AnimatedImage *>("first");
    auto *second = root->findChild<qextra::AnimatedImage *>("second");
    rstd_assert(still && first && second);
    rstd_assert(still->intrinsicSize() == QSize(0, 0));
    rstd_assert(still->property("intrinsicSize").toSize() == QSize(0, 0));
    QList<QSize> sizes;
    QObject::connect(still, &qextra::Image::intrinsicSizeChanged, still,
                     [&] { sizes.append(still->intrinsicSize()); });
    still->setSource(wide);
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    rstd_assert(still->sourceSize() == QSize(2, 2));
    rstd_assert(still->intrinsicSize() == QSize(4, 2));
    rstd_assert(sizes == QList<QSize>{QSize(4, 2)});

    const auto warmDecoded = qextra::image::service_statistics().decoded;
    still->setSource(QUrl());
    rstd_assert(still->intrinsicSize() == QSize(0, 0));
    still->setSource(wide);
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    rstd_assert(still->intrinsicSize() == QSize(4, 2));
    rstd_assert(qextra::image::service_statistics().decoded == warmDecoded);

    sizes.clear();
    still->setSourceSize(QSize(1, 1));
    rstd_assert(still->intrinsicSize() == QSize(4, 2));
    const bool reloaded =
        wait_for([&] { return still->status() == qextra::Image::Ready; });
    if (!reloaded) {
      const auto stats = qextra::image::service_statistics();
      qCritical() << "decode resize stalled" << still->status()
                  << still->errorString() << stats.running.to_primitive()
                  << stats.waiting.to_primitive()
                  << stats.decoded.to_primitive();
    }
    rstd_assert(reloaded);
    rstd_assert(still->sourceSize() == QSize(1, 1));
    rstd_assert(still->intrinsicSize() == QSize(4, 2));
    still->setMaxSize(QSize(1, 1));
    rstd_assert(still->intrinsicSize() == QSize(4, 2));
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    rstd_assert(sizes.isEmpty());

    still->setLoadingMode(qextra::Image::Suspended);
    still->setSource(tall);
    rstd_assert(still->intrinsicSize() == QSize(0, 0));
    rstd_assert(still->implicitWidth() > 0);
    still->setLoadingMode(qextra::Image::Automatic);
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    rstd_assert(still->intrinsicSize() == QSize(2, 6));
    rstd_assert(sizes == QList<QSize>({QSize(0, 0), QSize(2, 6)}));

    sizes.clear();
    auto sourceResize =
        QObject::connect(still, &qextra::Image::sourceChanged, still,
                         [&] { still->setSourceSize(QSize(2, 2)); });
    still->setSource(wide);
    rstd_assert(still->intrinsicSize() == QSize(0, 0));
    rstd_assert(sizes == QList<QSize>{QSize(0, 0)});
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    rstd_assert(still->intrinsicSize() == QSize(4, 2));
    QObject::disconnect(sourceResize);

    int firstChanges{}, secondChanges{};
    QObject::connect(first, &qextra::Image::intrinsicSizeChanged, first,
                     [&] { ++firstChanges; });
    QObject::connect(second, &qextra::Image::intrinsicSizeChanged, second,
                     [&] { ++secondChanges; });
    first->setSource(animation);
    second->setSource(animation);
    rstd_assert(wait_for([&] {
      return first->currentFrame() > 0 && second->currentFrame() > 0;
    }));
    rstd_assert(first->intrinsicSize() == QSize(2, 1));
    rstd_assert(second->intrinsicSize() == QSize(2, 1));
    rstd_assert(first->sourceSize() == QSize(1, 1));
    rstd_assert(firstChanges == 1 && secondChanges == 1);
    rstd_assert(qextra::image::playback_statistics(&engine).sessions ==
                usize(1));
    second->setSharedPlayback(false);
    rstd_assert(
        wait_for([&] { return second->status() == qextra::Image::Ready; }));
    rstd_assert(second->intrinsicSize() == QSize(2, 1));
    rstd_assert(secondChanges == 1);

    still->setSource(QUrl());
    bool replaced{};
    auto replacement = QObject::connect(
        still, &qextra::Image::intrinsicSizeChanged, still, [&] {
          if (!replaced && still->intrinsicSize() == QSize(4, 2)) {
            replaced = true;
            still->setSource(tall);
          }
        });
    QUrl frameSource;
    QObject::connect(
        still, &qextra::Image::frameReady, still,
        [&](const QUrl &source, const QImage &) { frameSource = source; });
    still->setSource(wide);
    rstd_assert(wait_for(
        [&] { return replaced && still->status() == qextra::Image::Ready; }));
    rstd_assert(still->source() == tall);
    rstd_assert(still->intrinsicSize() == QSize(2, 6));
    rstd_assert(frameSource == tall);
    QObject::disconnect(replacement);

    still->setLoadingMode(qextra::Image::Suspended);
    still->setSourceSize(QSize(-1, -1));
    still->setMaxSize(QSize(-1, -1));
    still->setSource(oriented);
    QImage orientedFrame;
    auto orientationFrame = QObject::connect(
        still, &qextra::Image::frameReady, still,
        [&](const QUrl &, const QImage &frame) { orientedFrame = frame; });
    still->setLoadingMode(qextra::Image::Automatic);
    rstd_assert(
        wait_for([&] { return still->status() == qextra::Image::Ready; }));
    rstd_assert(still->intrinsicSize() == orientedFrame.size());
    qInfo() << "EXIF orientation 6 source pixels" << still->intrinsicSize();
    QObject::disconnect(orientationFrame);
    orientedFrame = {};

    const auto transientComponent = QByteArray(R"(
import QtQuick
import QExtra as QE
QE.Image { width: 32; height: 32 }
)");
    QQmlComponent transient(&engine);
    transient.setData(transientComponent, QUrl("qrc:/metadata-transient.qml"));
    auto *image = qobject_cast<qextra::Image *>(transient.create());
    rstd_assert(image);
    QPointer<qextra::Image> guard(image);
    image->setParent(root);
    image->setParentItem(qobject_cast<QQuickWindow *>(root)->contentItem());
    QObject::connect(image, &qextra::Image::intrinsicSizeChanged, image,
                     [image] { delete image; });
    image->setSource(wide);
    rstd_assert(wait_for([&] { return !guard; }));

    first->setSource(QUrl());
    second->setSource(QUrl());
    still->setSource(QUrl());
    rstd_assert(still->intrinsicSize() == QSize(0, 0));
    delete root;
  }
  rstd_assert(
      wait_for([] { return qextra::image::payload_count() == usize(); }));
  qextra::image::clear_cache();
  rstd_assert(wait_for(
      [] { return qextra::image::statistics().live_bytes == usize(); }));
  return 0;
}
