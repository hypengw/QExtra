#include "QExtra/image.hpp"
#include "tests.hpp"
#include <memory>
#include <rstd/macro.hpp>
import rstd;
import qextra;
import ncrequest;

using namespace rstd::prelude;

namespace {
template <typename F> bool wait_for(F predicate, int timeout = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QThread::msleep(5);
  }
  return predicate();
}

class Manager : public QNetworkAccessManager {
public:
  int requests{};
  using QNetworkAccessManager::QNetworkAccessManager;
  QNetworkReply *createRequest(Operation op, const QNetworkRequest &original,
                               QIODevice *data) override {
    ++requests;
    return QNetworkAccessManager::createRequest(op, original, data);
  }
};
class Factory : public QQmlNetworkAccessManagerFactory {
public:
  QTemporaryDir cache;
  QPointer<Manager> manager;
  QNetworkAccessManager *create(QObject *parent) override {
    manager = new Manager(parent);
    auto *resources = new ncrequest::client::qt_network::ResourceCache(manager);
    resources->install_disk_cache(cache.path(), 50 * 1024 * 1024);
    return manager;
  }
};

class ControlledReply : public QNetworkReply {
public:
  ControlledReply(const QNetworkRequest &request, QObject *parent)
      : QNetworkReply(parent) {
    setRequest(request);
    setUrl(request.url());
    setOperation(QNetworkAccessManager::GetOperation);
    open(QIODevice::ReadOnly);
  }
  void complete(NetworkError error = NoError) {
    if (isFinished())
      return;
    setFinished(true);
    if (error != NoError) {
      setError(error, QStringLiteral("Controlled network error"));
      emit errorOccurred(error);
    }
    emit finished();
  }
  void abort() override { complete(OperationCanceledError); }
  qint64 readData(char *, qint64) override { return -1; }
};

class ControlledManager : public QNetworkAccessManager {
public:
  QList<QPointer<ControlledReply>> replies;
  QList<QUrl> started;
  int active{};
  int peak{};
  using QNetworkAccessManager::QNetworkAccessManager;
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request,
                               QIODevice *) override {
    auto *reply = new ControlledReply(request, this);
    replies.append(reply);
    started.append(request.url());
    peak = qMax(peak, ++active);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this] { --active; });
    return reply;
  }
};

class ControlledFactory : public QQmlNetworkAccessManagerFactory {
public:
  QPointer<ControlledManager> manager;
  QNetworkAccessManager *create(QObject *parent) override {
    manager = new ControlledManager(parent);
    return manager;
  }
};

void test_production_cache(const QUrl &first, const QUrl &second,
                           QHash<QByteArray, int> &hits) {
  QTemporaryDir directory;
  rstd_assert(directory.isValid());
  const QUrl urls[] = {first.resolved(QUrl("/production-cached-first")),
                       second.resolved(QUrl("/production-cached-second"))};
  const QByteArray paths[] = {"/production-cached-first",
                              "/production-cached-second"};
  const auto noStore = first.resolved(QUrl("/production-no-store"));
  const auto file = first.resolved(QUrl("/production-file"));
  auto download = [](QQmlEngine &engine, const QUrl &url) {
    qextra::image::NetworkLoad load(&engine, url, nullptr);
    rstd_assert(wait_for([&] { return load.done(); }));
    rstd_assert(load.error().isEmpty());
    rstd_assert(load.take_source().is_ok());
  };
  auto verifyFresh = [](QQmlEngine &engine, const QUrl &url) {
    auto *resources = ncrequest::client::qt_network::ResourceCache::get(
        engine.networkAccessManager());
    rstd_assert(resources);
    rstd_assert(wait_for([&] {
      return resources->acquire(QNetworkRequest(url)).snapshot().fresh();
    }));
  };
  {
    QmlNetworkDiskCache cache(directory.path(), 50 * 1024 * 1024);
    QQmlEngine engine;
    cache.install(engine);
    auto *disk = engine.networkAccessManager()->cache();
    rstd_assert(disk);
    for (int i = 0; i < 2; ++i) {
      download(engine, urls[i]);
      verifyFresh(engine, urls[i]);
      rstd_assert(hits[paths[i]] == 1);
      rstd_assert(wait_for([&] { return disk->metaData(urls[i]).isValid(); }));
      download(engine, urls[i]);
      verifyFresh(engine, urls[i]);
      rstd_assert(hits[paths[i]] == 1);
    }
    rstd_assert(wait_for([&] { return cache.cacheSize() > 0; }));
    download(engine, noStore);
    download(engine, noStore);
    rstd_assert(hits["/production-no-store"] == 2);
    rstd_assert(!disk->metaData(noStore).isValid());
    for (int i = 0; i < 2; ++i) {
      auto *reply = engine.networkAccessManager()->get(QNetworkRequest(file));
      rstd_assert(wait_for([&] { return reply->isFinished(); }));
      rstd_assert(reply->error() == QNetworkReply::NoError);
      rstd_assert(
          reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() ==
          200);
      rstd_assert(reply->readAll() == "local image response");
      reply->deleteLater();
      rstd_assert(hits["/production-file"] == i + 1);
      rstd_assert(!disk->metaData(file).isValid());
    }
  }
  {
    QmlNetworkDiskCache cache(directory.path(), 50 * 1024 * 1024);
    QQmlEngine engine;
    cache.install(engine);
    auto *disk = engine.networkAccessManager()->cache();
    rstd_assert(disk && cache.cacheSize() > 0);
    for (int i = 0; i < 2; ++i) {
      rstd_assert(disk->metaData(urls[i]).isValid());
      download(engine, urls[i]);
      verifyFresh(engine, urls[i]);
      rstd_assert(hits[paths[i]] == 1);
    }
    cache.clear();
    rstd_assert(wait_for([&] { return cache.cacheSize() == 0; }));
    for (int i = 0; i < 2; ++i) {
      rstd_assert(!disk->metaData(urls[i]).isValid());
      download(engine, urls[i]);
      verifyFresh(engine, urls[i]);
      rstd_assert(hits[paths[i]] == 2);
    }
  }
  qInfo() << "QmlNetworkDiskCache default origins, persistence, clear and "
             "response exclusions passed";
}

void test_all_origins_cache(const QUrl &first, const QUrl &second,
                            QHash<QByteArray, int> &hits) {
  Factory factory;
  QQmlEngine engine;
  engine.setNetworkAccessManagerFactory(&factory);
  rstd_assert(qextra::image::set_network_cache(&engine, {first}, 300));
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
  visible: true; width: 64; height: 64
  QE.Image { objectName: "image"; anchors.fill: parent }
})",
                    QUrl("qrc:/all-origins-cache.qml"));
  std::unique_ptr<QObject> root(component.create());
  rstd_assert(root);
  auto *image = root->findChild<qextra::Image *>("image");
  rstd_assert(image);
  auto *resources = ncrequest::client::qt_network::ResourceCache::get(
      engine.networkAccessManager());
  rstd_assert(resources && factory.manager);
  auto load = [&](const QUrl &url) {
    image->setSource(QUrl());
    image->setSource(url);
    rstd_assert(
        wait_for([&] { return image->status() == qextra::Image::Ready; }));
  };
  auto fresh = [&](const QUrl &url) {
    return resources->acquire(QNetworkRequest(url)).snapshot().fresh();
  };
  const auto allowed = first.resolved(QUrl("/all-origins-restricted-first"));
  const auto excluded = second.resolved(QUrl("/all-origins-restricted-second"));
  load(allowed);
  rstd_assert(fresh(allowed));
  load(excluded);
  rstd_assert(!fresh(excluded));
  image->setSource(QUrl());
  rstd_assert(qextra::image::set_network_cache(&engine, {}, 300, true));
  QCoreApplication::sendPostedEvents(image, QEvent::MetaCall);
  const QUrl urls[] = {first.resolved(QUrl("/all-origins-first")),
                       second.resolved(QUrl("/all-origins-second")),
                       second.resolved(QUrl("/all-origins-fallback"))};
  const QByteArray paths[] = {"/all-origins-first", "/all-origins-second",
                              "/all-origins-fallback"};
  for (int i = 0; i < 3; ++i) {
    load(urls[i]);
    rstd_assert(fresh(urls[i]) && hits[paths[i]] == 1);
    const auto requests = factory.manager->requests;
    const auto bytes = qextra::image::network_statistics(&engine).bytes;
    const auto decoded = qextra::image::service_statistics().decoded;
    load(urls[i]);
    rstd_assert(factory.manager->requests == requests && hits[paths[i]] == 1);
    rstd_assert(qextra::image::network_statistics(&engine).bytes == bytes);
    rstd_assert(qextra::image::service_statistics().decoded == decoded);
  }
  for (const auto &path : {QByteArray("/all-origins-no-store"),
                           QByteArray("/all-origins-no-cache")}) {
    const auto url = second.resolved(QUrl(QString::fromLatin1(path)));
    for (int request = 0; request < 2; ++request) {
      load(url);
      rstd_assert(!fresh(url) && hits[path] == request + 1);
    }
  }
  image->setSource(QUrl());
  qextra::image::clear_network_cache(&engine);
  QCoreApplication::sendPostedEvents(image, QEvent::MetaCall);
  for (int i = 0; i < 3; ++i) {
    rstd_assert(!fresh(urls[i]));
    load(urls[i]);
    rstd_assert(fresh(urls[i]) && hits[paths[i]] == 2);
  }
  qInfo()
      << "Image all origins, fallback, response directives and clear passed";
}

void test_queue() {
  ControlledFactory first, second;
  auto *engine = new QQmlEngine;
  engine->setNetworkAccessManagerFactory(&first);
  QQmlEngine other;
  other.setNetworkAccessManagerFactory(&second);
  QList<qextra::image::NetworkLoad *> loads, otherLoads;
  auto url = [](int index) {
    return QUrl(QStringLiteral("https://host%1.invalid/image%2")
                    .arg(index % 3)
                    .arg(index));
  };
  for (int i = 0; i < 12; ++i)
    loads.append(new qextra::image::NetworkLoad(engine, url(i), nullptr));
  rstd_assert(
      wait_for([&] { return first.manager && first.manager->active == 8; }));
  rstd_assert(first.manager->started.size() == 8 && first.manager->peak == 8);
  for (int i = 0; i < 8; ++i)
    rstd_assert(first.manager->started[i] == url(i));
  for (int i = 0; i < 9; ++i)
    otherLoads.append(new qextra::image::NetworkLoad(&other, url(i), nullptr));
  rstd_assert(
      wait_for([&] { return second.manager && second.manager->active == 8; }));
  rstd_assert(first.manager->active == 8);

  delete loads[1];
  loads[1] = nullptr;
  delete loads[10];
  loads[10] = nullptr;
  rstd_assert(wait_for([&] { return first.manager->started.size() == 9; }));
  rstd_assert(first.manager->started.last() == url(8));
  first.manager->replies[0]->complete();
  rstd_assert(wait_for([&] { return first.manager->started.size() == 10; }));
  rstd_assert(first.manager->started.last() == url(9));
  first.manager->replies[2]->complete(QNetworkReply::ContentNotFoundError);
  rstd_assert(wait_for([&] { return first.manager->started.size() == 11; }));
  rstd_assert(first.manager->started.last() == url(11));
  rstd_assert(!first.manager->started.contains(url(10)));
  rstd_assert(first.manager->peak == 8 && first.manager->active == 8);

  for (int i = 12; i < 15; ++i)
    loads.append(new qextra::image::NetworkLoad(engine, url(i), nullptr));
  delete engine;
  for (auto *load : loads) {
    if (load)
      rstd_assert(load->done());
    delete load;
  }
  for (auto *load : otherLoads)
    delete load;
  rstd_assert(second.manager->active == 0);
  const auto started = second.manager->started.size();
  QCoreApplication::processEvents();
  rstd_assert(second.manager->started.size() == started);
  ControlledFactory coalesced;
  QQmlEngine shared;
  shared.setNetworkAccessManagerFactory(&coalesced);
  auto *firstLoad = new qextra::image::NetworkLoad(&shared, url(30), nullptr);
  auto *secondLoad = new qextra::image::NetworkLoad(&shared, url(30), nullptr);
  rstd_assert(wait_for(
      [&] { return coalesced.manager && coalesced.manager->active == 1; }));
  rstd_assert(coalesced.manager->started.size() == 1);
  delete firstLoad;
  rstd_assert(coalesced.manager->active == 1);
  delete secondLoad;
  rstd_assert(coalesced.manager->active == 0);
  ControlledFactory priorityFactory;
  QQmlEngine priorityEngine;
  priorityEngine.setNetworkAccessManagerFactory(&priorityFactory);
  QList<qextra::image::NetworkLoad *> preparation;
  for (int i = 0; i < 10; ++i) {
    auto *load =
        new qextra::image::NetworkLoad(&priorityEngine, url(40 + i), nullptr);
    load->set_prefetch(true);
    preparation.append(load);
  }
  rstd_assert(wait_for([&] {
    return priorityFactory.manager && priorityFactory.manager->active == 6;
  }));
  rstd_assert(priorityFactory.manager->started.size() == 6);
  for (int i = 0; i < 6; ++i)
    rstd_assert(priorityFactory.manager->started[i] == url(40 + i));
  preparation[8]->set_prefetch(false);
  rstd_assert(wait_for([&] { return priorityFactory.manager->active == 7; }));
  rstd_assert(priorityFactory.manager->started.last() == url(48));
  auto *visibleLoad =
      new qextra::image::NetworkLoad(&priorityEngine, url(100), nullptr);
  rstd_assert(wait_for([&] { return priorityFactory.manager->active == 8; }));
  rstd_assert(priorityFactory.manager->started.count(url(48)) == 1);
  delete visibleLoad;
  qDeleteAll(preparation);
  rstd_assert(priorityFactory.manager->active == 0);
  qInfo()
      << "network queue engine isolation, limit, order and cancellation passed";
}

void test_queue_priority_fifo() {
  ControlledFactory factory;
  QQmlEngine engine;
  engine.setNetworkAccessManagerFactory(&factory);
  auto url = [](int index) {
    return QUrl(QStringLiteral("https://fifo.invalid/image%1").arg(index));
  };
  auto enqueue = [&](int index, bool prefetch) {
    auto *load = new qextra::image::NetworkLoad(&engine, url(index), nullptr);
    load->set_prefetch(prefetch);
    return load;
  };
  QList<qextra::image::NetworkLoad *> blockers, waiting;
  for (int i = 0; i < 8; ++i)
    blockers.append(enqueue(i, false));
  rstd_assert(wait_for(
      [&] { return factory.manager && factory.manager->active == 8; }));
  for (int i = 0; i < 8; ++i)
    rstd_assert(factory.manager->started[i] == url(i));
  for (int i = 0; i < 6; ++i)
    waiting.append(enqueue(100 + i, i % 2 == 0));
  waiting[2]->set_prefetch(false);
  auto *sharedVisible = enqueue(100, false);
  delete sharedVisible;
  delete waiting[3];
  waiting[3] = nullptr;
  const int visible[] = {101, 102, 105};
  for (int i = 0; i < 3; ++i) {
    factory.manager->replies[i]->complete();
    rstd_assert(
        wait_for([&] { return factory.manager->started.size() == 9 + i; }));
    rstd_assert(factory.manager->started.last() == url(visible[i]));
    rstd_assert(factory.manager->active == 8);
  }
  for (int i = 3; i < 5; ++i) {
    factory.manager->replies[i]->complete();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    rstd_assert(factory.manager->started.size() == 11);
    rstd_assert(factory.manager->active == 10 - i);
  }
  factory.manager->replies[5]->complete();
  rstd_assert(wait_for([&] { return factory.manager->started.size() == 12; }));
  rstd_assert(factory.manager->started.last() == url(100));
  rstd_assert(factory.manager->active == 6);
  factory.manager->replies[6]->complete();
  rstd_assert(wait_for([&] { return factory.manager->started.size() == 13; }));
  rstd_assert(factory.manager->started.last() == url(104));
  rstd_assert(factory.manager->active == 6 && factory.manager->peak == 8);
  rstd_assert(factory.manager->started.count(url(100)) == 1);
  rstd_assert(!factory.manager->started.contains(url(103)));
  qDeleteAll(blockers);
  qDeleteAll(waiting);
  rstd_assert(factory.manager->active == 0);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  rstd_assert(factory.manager->started.size() == 13);
  qInfo() << "network queue priority FIFO, stable promotion and shared "
             "subscriber cancellation passed";
}
} // namespace

namespace {
void test_gpu_cache(const QUrl &base, QHash<QByteArray, int> &hits,
                    bool &blue) {
  Factory factory;
  QQmlEngine engine;
  engine.setNetworkAccessManagerFactory(&factory);
  rstd_assert(qextra::image::set_network_cache(&engine, {base}, 300));
  auto *resources = ncrequest::client::qt_network::ResourceCache::get(
      engine.networkAccessManager());
  rstd_assert(resources);
  ncrequest::client::qt_network::ResourceOptions options;
  options.origins = {base};
  options.fallback_seconds = 300;
  options.maximum_entries = 2;
  resources->configure(options);
  qextra::image::set_cache_limit(usize());
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
  visible: true; width: 64; height: 64; color: "blue"
  Item {
    id: scope; anchors.fill: parent; clip: true
    QE.Image {
      objectName: "display"; width: 64; height: 64
      preparationScope: scope
      frameDataRequired: false
    }
  }
})",
                    QUrl("qrc:/gpu-cache.qml"));
  std::unique_ptr<QObject> root(component.create());
  rstd_assert(root);
  auto *window = qobject_cast<QQuickWindow *>(root.get());
  auto *image = root->findChild<qextra::Image *>("display");
  rstd_assert(window && image);
  rstd_assert(qextra::image::set_texture_cache_limit(window, 1024 * 1024));
  const auto url = base.resolved(QUrl("/gpu-cached"));
  auto texture = [&] { return qextra::image::texture_statistics(window); };
  auto ready = [&] {
    if (image->status() != qextra::Image::Ready)
      return false;
    const auto pixels = image->window()->grabWindow();
    if (pixels.isNull())
      return false;
    const auto color =
        pixels.pixelColor(pixels.width() / 2, pixels.height() / 2);
    return image->hasFrame() && color.red() > 240 && color.blue() < 10;
  };
  auto idle = [&] {
    image->setSource(QUrl());
    rstd_assert(wait_for([&] {
      window->grabWindow();
      return texture().active_bytes == 0 && texture().idle_bytes > 0;
    }));
    rstd_assert(!image->hasFrame());
  };
  int frames{};
  QObject::connect(image, &qextra::Image::frameReady, image,
                   [&](const QUrl &, const QImage &pixels) {
                     rstd_assert(!pixels.isNull());
                     ++frames;
                   });
  image->setSource(url);
  rstd_assert(wait_for(ready));
  rstd_assert(image->intrinsicSize() == QSize(1, 1));
  idle();
  const auto initialFrames = frames;
  const auto decoded = qextra::image::service_statistics().decoded;
  const auto network = qextra::image::network_statistics(&engine);
  const auto uploaded = texture().uploads;
  const auto allocated = texture().allocations;
  const auto requests = factory.manager->requests;
  const auto wire = hits["/gpu-cached"];
  QElapsedTimer warmTime;
  warmTime.start();
  image->setSource(url);
  rstd_assert(wait_for(ready));
  qInfo()
      << "GPU-only warm first-frame ms" << warmTime.elapsed() << "Qt requests"
      << factory.manager->requests - requests << "wire requests"
      << hits["/gpu-cached"] - wire << "body bytes"
      << qextra::image::network_statistics(&engine).bytes - network.bytes
      << "decoded"
      << (qextra::image::service_statistics().decoded - decoded).to_primitive()
      << "uploads" << texture().uploads - uploaded << "allocations"
      << texture().allocations - allocated;
  rstd_assert(qextra::image::service_statistics().cache_bytes == usize());
  rstd_assert(qextra::image::service_statistics().decoded == decoded);
  rstd_assert(factory.manager->requests == requests &&
              hits["/gpu-cached"] == wire);
  rstd_assert(qextra::image::network_statistics(&engine).bytes ==
              network.bytes);
  rstd_assert(texture().uploads == uploaded &&
              texture().allocations == allocated);
  rstd_assert(frames == initialFrames);
  idle();
  for (int i = 0; i < 4; ++i) {
    qextra::image::NetworkLoad load(
        &engine, base.resolved(QUrl(QStringLiteral("/gpu-other-%1").arg(i))),
        nullptr);
    rstd_assert(wait_for([&] { return load.done(); }));
    rstd_assert(load.error().isEmpty() && load.take_source().is_ok());
  }
  rstd_assert(resources->statistics().index_evictions > 0);
  auto opened = factory.manager->requests;
  image->setX(128);
  image->setLoadingMode(qextra::Image::Prefetch);
  image->setSource(url);
  QCoreApplication::sendPostedEvents(image, QEvent::MetaCall);
  window->grabWindow();
  rstd_assert(image->status() == qextra::Image::Loading && !image->hasFrame());
  rstd_assert(factory.manager->requests == opened);
  image->setX(0);
  image->setLoadingMode(qextra::Image::Automatic);
  rstd_assert(wait_for(ready));
  rstd_assert(factory.manager->requests == opened);
  rstd_assert(qextra::image::service_statistics().decoded == decoded);
  rstd_assert(texture().uploads == uploaded &&
              texture().allocations == allocated);
  rstd_assert(resources->statistics().retained_hits > 0);

  idle();
  image->resetWidth();
  image->resetHeight();
  rstd_assert(image->size().isEmpty());
  image->setSource(url);
  rstd_assert(wait_for([&] {
    const auto pixels = window->grabWindow();
    return image->status() == qextra::Image::Ready && image->hasFrame() &&
           image->size() == QSizeF(1, 1) && !pixels.isNull() &&
           pixels.pixelColor(0, 0).red() > 240;
  }));
  rstd_assert(factory.manager->requests == opened);
  rstd_assert(qextra::image::service_statistics().decoded == decoded);
  image->setWidth(64);
  image->setHeight(64);

  // CPU consumers explicitly request frame data even when a texture is
  // resident.
  idle();
  image->setSource(url);
  QCoreApplication::sendPostedEvents(image, QEvent::MetaCall);
  image->setFrameDataRequired(true);
  rstd_assert(wait_for([&] { return ready() && frames > initialFrames; }));
  rstd_assert(factory.manager->requests == opened + 1);
  rstd_assert(hits["/gpu-cached"] == wire);
  rstd_assert(qextra::image::service_statistics().decoded ==
              decoded + usize(1));
  rstd_assert(texture().uploads == uploaded);
  idle();
  image->setFrameDataRequired(false);
  qextra::image::trim_texture_cache(window);
  rstd_assert(wait_for([&] {
    window->grabWindow();
    return texture().idle_bytes == 0;
  }));
  const auto beforeMiss = qextra::image::service_statistics().decoded;
  image->setSource(url);
  rstd_assert(wait_for(ready));
  rstd_assert(qextra::image::service_statistics().decoded ==
              beforeMiss + usize(1));
  rstd_assert(texture().uploads == uploaded + 1);
  idle();

  // A successful validation must keep the replacement source record alive.
  image->setFrameDataRequired(true);
  const auto validated = base.resolved(QUrl("/gpu-validated"));
  image->setSource(validated);
  rstd_assert(wait_for(ready));
  idle();
  image->setSource(validated);
  rstd_assert(wait_for(ready));
  rstd_assert(hits["/gpu-validated"] == 2);
  idle();
  image->setFrameDataRequired(false);
  for (int i = 4; i < 8; ++i) {
    qextra::image::NetworkLoad load(
        &engine, base.resolved(QUrl(QStringLiteral("/gpu-other-%1").arg(i))),
        nullptr);
    rstd_assert(wait_for([&] { return load.done(); }));
    rstd_assert(load.error().isEmpty() && load.take_source().is_ok());
  }
  opened = factory.manager->requests;
  const auto renewedDecoded = qextra::image::service_statistics().decoded;
  const auto renewedUploads = texture().uploads;
  image->setSource(validated);
  rstd_assert(wait_for(ready));
  rstd_assert(factory.manager->requests == opened &&
              hits["/gpu-validated"] == 2);
  rstd_assert(qextra::image::service_statistics().decoded == renewedDecoded);
  rstd_assert(texture().uploads == renewedUploads);
  qInfo() << "GPU-only validated/evicted deltas"
          << factory.manager->requests - opened
          << (qextra::image::service_statistics().decoded - renewedDecoded)
                 .to_primitive()
          << texture().uploads - renewedUploads;

  image->setSource(QUrl());
  qextra::image::trim_texture_cache(window);
  rstd_assert(wait_for([&] {
    window->grabWindow();
    return texture().idle_bytes == 0 && texture().active_bytes == 0;
  }));
  QSharedPointer<qextra::image::DisplayHandle> sharedDisplay;
  quint64 sharedRevision{};
  const auto sharedUrl = base.resolved(QUrl("/gpu-shared-validated"));
  {
    qextra::image::NetworkLoad first(&engine, sharedUrl, nullptr);
    rstd_assert(wait_for([&] { return first.done(); }));
    auto oldSource = first.snapshot();
    auto firstInput = first.take_source().unwrap();
    auto firstRequest = qextra::image::Request::open_source(
                            oldSource.identity(), firstInput.clone(), {}, false,
                            true, 999, 0, oldSource)
                            .unwrap();
    qextra::image::NetworkLoad second(&engine, sharedUrl, nullptr);
    rstd_assert(wait_for([&] { return second.done(); }));
    auto newSource = second.snapshot();
    sharedRevision = newSource.revision();
    rstd_assert(newSource.identity() == oldSource.identity() &&
                newSource.revision() > oldSource.revision());
    auto secondRequest =
        qextra::image::Request::open_source(newSource.identity(),
                                            second.take_source().unwrap(), {},
                                            false, true, 999, 0, newSource)
            .unwrap();
    auto lateOldRequest = qextra::image::Request::open_source(
                              oldSource.identity(), rstd::move(firstInput), {},
                              false, true, 999, 0, oldSource)
                              .unwrap();
    rstd_assert(firstRequest.same_resource(secondRequest) &&
                firstRequest.same_resource(lateOldRequest));
    rstd_assert(firstRequest.advance());
    rstd_assert(wait_for([&] {
      auto frame = firstRequest.poll().frame;
      if (!frame)
        return false;
      sharedDisplay = (*frame)->display;
      return bool(sharedDisplay);
    }));
    sharedDisplay->retain_source(oldSource);
  }
  for (int i = 8; i < 12; ++i) {
    qextra::image::NetworkLoad load(
        &engine, base.resolved(QUrl(QStringLiteral("/gpu-other-%1").arg(i))),
        nullptr);
    rstd_assert(wait_for([&] { return load.done(); }));
    rstd_assert(load.error().isEmpty() && load.take_source().is_ok());
  }
  auto retained = resources->acquire(QNetworkRequest(sharedUrl));
  rstd_assert(retained.snapshot().fresh() &&
              retained.snapshot().revision() == sharedRevision);
  sharedDisplay.clear();

  image->setSource(QUrl());
  image->setSource(url);
  rstd_assert(wait_for(ready));
  idle();
  image->setSource(url);
  rstd_assert(wait_for(ready));
  const auto priorContext = texture();
  if (qEnvironmentVariable("QSG_RENDER_LOOP") == QLatin1String("basic")) {
    auto *parent = image->parentItem();
    QQuickWindow next;
    next.resize(64, 64);
    next.setColor(Qt::blue);
    rstd_assert(qextra::image::set_texture_cache_limit(&next, 1024 * 1024));
    next.show();
    image->setParentItem(next.contentItem());
    rstd_assert(wait_for(ready));
    const auto moved = qextra::image::texture_statistics(&next);
    rstd_assert(image->window() == &next && moved.allocations == 1 &&
                moved.uploads == 1);
    image->setParentItem(parent);
    rstd_assert(wait_for(ready));
    rstd_assert(image->window() == window);
    qInfo() << "GPU-only window restore allocations" << moved.allocations
            << "uploads" << moved.uploads;
  } else {
    window->setPersistentSceneGraph(false);
    window->setPersistentGraphics(false);
    window->hide();
    window->releaseResources();
    rstd_assert(wait_for(
        [&] { return texture().generation > priorContext.generation; }));
    window->show();
    rstd_assert(wait_for(ready));
    const auto restored = texture();
    rstd_assert(restored.generation > priorContext.generation);
    rstd_assert(restored.uploads > priorContext.uploads);
    qInfo() << "GPU-only context restore generation" << priorContext.generation
            << restored.generation << "uploads" << priorContext.uploads
            << restored.uploads;
  }

  blue = false;
  const auto changing = base.resolved(QUrl("/gpu-changing"));
  image->setSource(changing);
  rstd_assert(wait_for(ready));
  idle();
  image->setSource(changing);
  rstd_assert(wait_for(ready));
  const auto beforeClear = texture();
  const auto beforeClearRequests = hits["/gpu-changing"];
  blue = true;
  qextra::image::clear_network_cache(&engine);
  rstd_assert(wait_for([&] {
    const auto pixels = window->grabWindow();
    return image->status() == qextra::Image::Ready && image->hasFrame() &&
           !pixels.isNull() &&
           pixels.pixelColor(pixels.width() / 2, pixels.height() / 2).blue() >
               240 &&
           texture().uploads > beforeClear.uploads;
  }));
  rstd_assert(hits["/gpu-changing"] == beforeClearRequests + 1);
  blue = false;

  image->setSource(QUrl());
  qextra::image::trim_texture_cache(window);
  rstd_assert(wait_for([&] {
    window->grabWindow();
    return texture().idle_bytes == 0 && texture().active_bytes == 0;
  }));
  const auto noStore = base.resolved(QUrl("/gpu-no-store"));
  image->setSource(noStore);
  rstd_assert(wait_for(ready));
  image->setSource(QUrl());
  rstd_assert(wait_for([&] {
    window->grabWindow();
    return texture().idle_bytes == 0 && texture().active_bytes == 0;
  }));
  const auto noStoreRequests = hits["/gpu-no-store"];
  image->setSource(noStore);
  rstd_assert(wait_for(ready));
  rstd_assert(hits["/gpu-no-store"] == noStoreRequests + 1);
  root.reset();
  qextra::image::set_cache_limit(usize(16 * 1024 * 1024));
  qInfo() << "GPU-only HTTP cache, metadata eviction, prefetch and pixel "
             "demand passed";
}
} // namespace

int run_image_network(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  test_queue();
  test_queue_priority_fifo();
  QTemporaryDir files, outside;
  rstd_assert(files.isValid() && outside.isValid());
  auto write = [](QString path, QByteArray bytes) {
    QFile file(path);
    rstd_assert(file.open(QIODevice::WriteOnly));
    rstd_assert(file.write(bytes) == bytes.size());
  };
  const auto fileImage = files.filePath("image.ppm");
  write(fileImage,
        QByteArray("P6\n1 1\n255\n") + QByteArray::fromHex("ff0000"));
  const auto outsideImage = outside.filePath("outside.ppm");
  write(outsideImage,
        QByteArray("P6\n1 1\n255\n") + QByteArray::fromHex("0000ff"));
  write(files.filePath("bad.ppm"), "not an image");
  rstd_assert(QFile::link(outsideImage, files.filePath("escape.ppm")));
  QByteArray gif;
  QList<QByteArray> animations;
  for (int i = 1; i < argc; ++i) {
    QFile file(QString::fromLocal8Bit(argv[i]));
    rstd_assert(file.open(QIODevice::ReadOnly));
    animations.push_back(file.readAll());
  }
  QTcpServer server, secondServer;
  rstd_assert(server.listen(QHostAddress::LocalHost));
  rstd_assert(secondServer.listen(QHostAddress::LocalHost));
  QHash<QByteArray, int> hits;
  bool blue{};
  auto url = [&](const char *path) {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                    .arg(server.serverPort())
                    .arg(QLatin1String(path)));
  };
  auto accept = [&](QTcpServer &listener) {
    while (auto *socket = listener.nextPendingConnection()) {
      QObject::connect(socket, &QTcpSocket::disconnected, socket,
                       &QObject::deleteLater);
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
        auto request =
            socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n") ||
            socket->property("handled").toBool())
          return;
        socket->setProperty("handled", true);
        const auto path = request.split(' ').value(1);
        ++hits[path];
        if ((path == "/validated" || path == "/gpu-validated" ||
             path == "/gpu-shared-validated") &&
            request.contains("If-None-Match: \"image-1\"")) {
          socket->write("HTTP/1.1 304 Not Modified\r\nETag: "
                        "\"image-1\"\r\nCache-Control: public, "
                        "max-age=3600\r\nConnection: close\r\n\r\n");
          socket->disconnectFromHost();
          return;
        }
        if (path == "/slow")
          return;
        QByteArray body("P6\n1 1\n255\n");
        body += (path == "/changing" || path == "/gpu-changing") && blue
                    ? QByteArray::fromHex("0000ff")
                    : QByteArray::fromHex("ff0000");
        QByteArray status("200 OK"), extra;
        if (path.startsWith("/file") || path == "/production-file" ||
            path == "/outside" || path == "/escape") {
          auto target = path == "/outside"  ? outsideImage
                        : path == "/escape" ? files.filePath("escape.ppm")
                        : path == "/file-missing"
                            ? files.filePath("missing.ppm")
                        : path == "/file-bad" ? files.filePath("bad.ppm")
                                              : fileImage;
          extra = "QExtra-Image-Source: " +
                  QUrl::fromLocalFile(target).toEncoded() + "\r\n";
          body = path == "/production-file" ? QByteArray("local image response")
                                            : QByteArray();
          if (path == "/file-error")
            status = "404 Not Found";
          if (path == "/file-duplicate")
            extra += extra;
          if (path == "/file-query")
            extra = "QExtra-Image-Source: " +
                    QUrl::fromLocalFile(target).toEncoded() + "?query\r\n";
          if (path == "/file-relative")
            extra = "QExtra-Image-Source: image.ppm\r\n";
          if (path == "/file-long")
            extra = "QExtra-Image-Source: file:///" + QByteArray(16384, 'x') +
                    "\r\n";
          if (path == "/file-slow") {
            socket->write("HTTP/1.1 200 OK\r\n" + extra +
                          "Content-Length: 100000\r\n\r\n");
            return;
          }
        }
        if (path == "/redirect-file" || path == "/redirect-loop") {
          status = "302 Found";
          extra = path == "/redirect-file" ? "Location: /file\r\n"
                                           : "Location: /redirect-loop\r\n";
          body.clear();
        }
        if (path == "/redirect-external" || path == "/return-local") {
          auto target =
              url(path == "/redirect-external" ? "/return-local" : "/file");
          if (path == "/redirect-external")
            target.setHost(QStringLiteral("localhost"));
          status = "302 Found";
          extra = "Location: " + target.toEncoded() + "\r\n";
          body.clear();
        }
        if (path == "/gif")
          body = gif;
        if (path == "/cached" || path.startsWith("/production-cached-"))
          body = QByteArray("P6\n350 350\n255\n") +
                 QByteArray(350 * 350 * 3, char(255));
        if (path == "/large" || path == "/progress")
          body = QByteArray("P6\n300 100\n255\n") +
                 QByteArray(300 * 100 * 3, char(255));
        if (path == "/bad")
          body = "not an image";
        if (path == "/missing")
          status = "404 Not Found";
        if (path == "/redirect") {
          status = "302 Found";
          extra = "Location: /still\r\n";
          body = "redirect body";
        }
        if (path == "/unsafe") {
          status = "302 Found";
          extra = "Location: file:///nonexistent-network-image\r\n";
        }
        if (path == "/huge") {
          socket->write("HTTP/1.1 200 OK\r\nContent-Length: 67108865\r\n\r\n");
          return;
        }
        if (path == "/progress") {
          socket->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                        QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body.left(32000));
          QTimer::singleShot(100, socket, [socket, tail = body.mid(32000)] {
            socket->write(tail);
            socket->disconnectFromHost();
          });
          return;
        }
        if (path == "/chunked") {
          socket->write("HTTP/1.1 200 OK\r\nTransfer-Encoding: "
                        "chunked\r\nConnection: close\r\n\r\n");
          for (const auto &chunk : {body.left(5), body.mid(5)})
            socket->write(QByteArray::number(chunk.size(), 16) + "\r\n" +
                          chunk + "\r\n");
          socket->write("0\r\n\r\n");
        } else {
          if (path == "/all-origins-no-cache")
            extra += "Cache-Control: no-cache, max-age=3600\r\n";
          else if (path == "/validated" || path == "/gpu-validated" ||
                   path == "/gpu-shared-validated")
            extra += "ETag: \"image-1\"\r\nCache-Control: no-cache\r\n";
          else if (path != "/fallback" && path != "/all-origins-fallback")
            extra +=
                path == "/cached" || path.startsWith("/production-cached-") ||
                        (path.startsWith("/gpu-") && path != "/gpu-no-store") ||
                        (path.startsWith("/all-origins-") &&
                         path != "/all-origins-no-store") ||
                        path == "/production-file"
                    ? "Cache-Control: public, max-age=3600\r\n"
                    : "Cache-Control: no-store\r\n";
          socket->write("HTTP/1.1 " + status + "\r\n" + extra +
                        "Content-Length: " + QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body);
        }
        socket->disconnectFromHost();
      });
    }
  };
  QObject::connect(&server, &QTcpServer::newConnection, &server,
                   [&] { accept(server); });
  QObject::connect(&secondServer, &QTcpServer::newConnection, &secondServer,
                   [&] { accept(secondServer); });
  test_production_cache(
      url(""),
      QUrl(
          QStringLiteral("http://127.0.0.1:%1").arg(secondServer.serverPort())),
      hits);
  test_all_origins_cache(
      url(""),
      QUrl(
          QStringLiteral("http://127.0.0.1:%1").arg(secondServer.serverPort())),
      hits);
  test_gpu_cache(url(""), hits, blue);
  Factory factory;
  auto *engine = new QQmlEngine;
  engine->setNetworkAccessManagerFactory(&factory);
  rstd_assert(qextra::image::set_network_cache(engine, {url("")}, 300));
  QQmlComponent component(engine);
  component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
    visible: true; width: 64; height: 64
    QE.AnimatedImage { objectName: "image"; anchors.fill: parent; maxSize: Qt.size(512, 512) }
    QE.AnimatedImage { objectName: "peer"; anchors.fill: parent; maxSize: Qt.size(512, 512); sharedPlayback: true; visible: false }
    Item {
        width: 64; height: 64
        transform: Translate { objectName: "offset"; x: 128 }
        QE.Image { objectName: "transformed"; anchors.fill: parent }
    }
})",
                    QUrl("qrc:/network.qml"));
  auto *root = component.create();
  if (!root) {
    qCritical() << component.errors();
    return 1;
  }
  auto *window = qobject_cast<QQuickWindow *>(root);
  auto *image = root->findChild<qextra::AnimatedImage *>("image");
  auto *peer = root->findChild<qextra::AnimatedImage *>("peer");
  rstd_assert(wait_for([&] { return window->isExposed(); }));
  auto load = [&](const char *path,
                  qextra::Image::Status expected = qextra::Image::Ready) {
    image->setSource(QUrl());
    image->setSource(url(path));
    const bool ready = wait_for([&] { return image->status() == expected; });
    if (!ready) {
      qCritical() << path << image->status() << image->errorString();
      qCritical() << "server requests" << hits << "manager requests"
                  << factory.manager->requests;
      for (auto *reply : factory.manager->findChildren<QNetworkReply *>())
        qCritical() << reply->url() << reply->isFinished()
                    << reply->bytesAvailable() << reply->errorString();
    }
    rstd_assert(ready);
  };
  auto color = [&](bool expectBlue) {
    return wait_for([&] {
      auto pixels = window->grabWindow();
      if (pixels.isNull())
        return false;
      auto c = pixels.pixelColor(pixels.width() / 2, pixels.height() / 2);
      return expectBlue ? c.blue() > 240 && c.red() < 10
                        : c.red() > 240 && c.blue() < 10;
    });
  };
  load("/file", qextra::Image::Error);
  rstd_assert(
      qextra::image::set_local_file_source(engine, url(""), files.path()));
  load("/file");
  rstd_assert(image->source() == url("/file") && color(false));
  load("/file-slow");
  rstd_assert(color(false));
  load("/redirect-file");
  rstd_assert(color(false));
  load("/file-missing", qextra::Image::Error);
  load("/file-bad", qextra::Image::Error);
  load("/file-error", qextra::Image::Error);
  load("/file-duplicate", qextra::Image::Error);
  load("/file-query", qextra::Image::Error);
  load("/file-relative", qextra::Image::Error);
  load("/file-long", qextra::Image::Error);
  load("/redirect-external", qextra::Image::Error);
  load("/outside", qextra::Image::Error);
  load("/escape", qextra::Image::Error);
  load("/redirect-loop", qextra::Image::Error);
  write(fileImage,
        QByteArray("P6\n1 1\n255\n") + QByteArray::fromHex("0000ff"));
  load("/file");
  rstd_assert(color(true));
  rstd_assert(qextra::image::set_local_file_source(engine, QUrl(), QString()));
  load("/file", qextra::Image::Error);
  load("/still");
  rstd_assert(factory.manager && factory.manager->requests > 0);
  rstd_assert(image->sourceSize() == QSize(1, 1) && image->progress() == 1);
  rstd_assert(color(false));
  load("/redirect");
  rstd_assert(color(false));
  load("/chunked");
  rstd_assert(color(false));
  load("/large");
  rstd_assert(image->sourceSize() == QSize(300, 100));
  bool partialProgress = false;
  QObject::connect(image, &qextra::Image::progressChanged, image, [&] {
    partialProgress |= image->progress() > 0 && image->progress() < 1;
  });
  load("/progress");
  rstd_assert(partialProgress);
  load("/cached");
  const auto cached = hits["/cached"];
  const auto cachedRequests = factory.manager->requests;
  const auto decoded = qextra::image::service_statistics().decoded;
  load("/cached");
  rstd_assert(hits["/cached"] == cached);
  rstd_assert(factory.manager->requests == cachedRequests);
  rstd_assert(qextra::image::service_statistics().decoded == decoded);
  {
    const auto before = qextra::image::network_statistics(engine).requests;
    peer->setVisible(true);
    peer->setSource(url("/cached"));
    const bool ready =
        wait_for([&] { return peer->status() == qextra::Image::Ready; });
    if (!ready) {
      const auto playback = qextra::image::playback_statistics(engine);
      const auto service = qextra::image::service_statistics();
      qWarning() << "cached shared image stalled" << peer->status() << "network"
                 << qextra::image::network_statistics(engine).requests - before
                 << "decoded" << (service.decoded - decoded).to_primitive()
                 << "sessions" << playback.sessions.to_primitive() << "active"
                 << playback.active.to_primitive() << "pumps" << playback.pumps
                 << "running" << service.running.to_primitive() << "waiting"
                 << service.waiting.to_primitive();
    }
    rstd_assert(ready);
    rstd_assert(qextra::image::network_statistics(engine).requests == before);
    rstd_assert(qextra::image::service_statistics().decoded == decoded);
    peer->setSource(QUrl());
    peer->setVisible(false);
  }
  const auto pixelRequests = factory.manager->requests;
  qextra::image::clear_cache();
  load("/cached");
  rstd_assert(factory.manager->requests == pixelRequests + 1);
  rstd_assert(hits["/cached"] == cached);
  load("/fallback");
  const auto fallbackRequests = factory.manager->requests;
  load("/fallback");
  rstd_assert(factory.manager->requests == fallbackRequests);
  rstd_assert(hits["/fallback"] == 1);
  load("/validated");
  const auto validatedBytes = qextra::image::network_statistics(engine).bytes;
  const auto validatedDecoded = qextra::image::service_statistics().decoded;
  load("/validated");
  rstd_assert(hits["/validated"] == 2);
  rstd_assert(qextra::image::network_statistics(engine).bytes ==
              validatedBytes);
  rstd_assert(qextra::image::service_statistics().decoded == validatedDecoded);
  const auto validatedRequests = factory.manager->requests;
  load("/validated");
  rstd_assert(factory.manager->requests == validatedRequests);
  const auto resetRequests = factory.manager->requests;
  qextra::image::clear_network_cache(engine);
  rstd_assert(wait_for([&] {
    return factory.manager->requests > resetRequests &&
           image->status() == qextra::Image::Ready;
  }));
  load("/changing");
  rstd_assert(color(false));
  blue = true;
  load("/changing");
  rstd_assert(color(true));
  load("/missing", qextra::Image::Error);
  load("/bad", qextra::Image::Error);
  load("/huge", qextra::Image::Error);
  load("/unsafe", qextra::Image::Error);
  image->setSource(url("/slow"));
  rstd_assert(wait_for([&] { return hits["/slow"] == 1; }));
  load("/still");
  rstd_assert(color(false));
  image->setVisible(false);
  const auto before = hits["/still"];
  image->setSource(QUrl());
  image->setSource(url("/still"));
  wait_for([] { return false; }, 100);
  rstd_assert(hits["/still"] == before);
  image->setVisible(true);
  rstd_assert(
      wait_for([&] { return image->status() == qextra::Image::Ready; }));
  auto *transformed = root->findChild<qextra::Image *>("transformed");
  auto *offset = root->findChild<QObject *>("offset");
  transformed->setSource(url("/still"));
  wait_for([] { return false; }, 100);
  const auto beforeTransform = factory.manager->requests;
  offset->setProperty("x", 0);
  for (int i = 0; i < 8; ++i)
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  rstd_assert(factory.manager->requests == beforeTransform + 1);
  rstd_assert(
      wait_for([&] { return transformed->status() == qextra::Image::Ready; }));
  transformed->setSource(QUrl());
  for (const auto &animation : animations) {
    gif = animation;
    image->setPlaying(true);
    load("/gif");
    rstd_assert(wait_for([&] { return image->currentFrame() > 0; }));
    const auto downloads = hits["/gif"];
    image->restart();
    rstd_assert(wait_for([&] { return image->currentFrame() == 0; }));
    image->setCurrentFrame(1);
    rstd_assert(wait_for([&] { return image->currentFrame() == 1; }));
    image->setCache(false);
    qextra::image::clear_cache();
    image->restart();
    rstd_assert(wait_for([&] { return image->currentFrame() == 0; }));
    rstd_assert(wait_for([&] { return image->currentFrame() > 0; }));
    rstd_assert(hits["/gif"] == downloads);
  }
  image->setSource(url("/slow"));
  rstd_assert(wait_for([&] { return hits["/slow"] == 2; }));
  image->setSource(QUrl());
  image->setPlaying(true);
  image->setSharedPlayback(true);
  image->setCache(false);
  peer->setVisible(true);
  peer->setCache(false);
  for (const auto &animation : animations) {
    gif = animation;
    const auto opened = qextra::image::service_statistics().opened;
    image->setSource(url("/gif"));
    peer->setSource(url("/gif"));
    rstd_assert(wait_for([&] {
      return image->status() == qextra::Image::Ready &&
             peer->status() == qextra::Image::Ready;
    }));
    rstd_assert(qextra::image::playback_statistics(engine).sessions ==
                usize(1));
    rstd_assert(qextra::image::service_statistics().opened ==
                opened + usize(1));
    rstd_assert(wait_for([&] {
      return image->currentFrame() > 0 &&
             image->currentFrame() == peer->currentFrame();
    }));
    image->setSource(QUrl());
    rstd_assert(qextra::image::playback_statistics(engine).active == usize(1));
    const auto frame = peer->currentFrame();
    rstd_assert(wait_for([&] { return peer->currentFrame() != frame; }));
    peer->setSource(QUrl());
  }
  blue = false;
  load("/changing");
  blue = true;
  peer->setSource(url("/changing"));
  rstd_assert(wait_for([&] { return peer->status() == qextra::Image::Ready; }));
  rstd_assert(qextra::image::playback_statistics(engine).sessions == usize(2));
  rstd_assert(color(true));
  peer->setVisible(false);
  rstd_assert(color(false));
  peer->setSource(QUrl());
  image->setSource(url("/slow"));
  rstd_assert(wait_for([&] { return hits["/slow"] == 3; }));
  delete engine;
  rstd_assert(
      wait_for([&] { return image->status() == qextra::Image::Error; }));
  delete root;
  qextra::image::clear_cache();
  rstd_assert(wait_for(
      [] { return qextra::image::statistics().live_bytes == usize(); }));
  qInfo() << "network image lifecycle, cache and animation passed";
  return 0;
}
