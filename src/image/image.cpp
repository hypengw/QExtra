#include "QExtra/image.hpp"
#include "texture.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QRunnable>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlInfo>
#include <QtQuick/QSGImageNode>
#include <QtQuick/QSGRendererInterface>

import rstd;
import qextra.image.service;
import qextra.image.network;
import qextra.image.playback;

using namespace rstd::prelude;
using rstd::sync::Arc;
namespace wi = wavsen::image;

namespace qextra {

ImageCache::ImageCache(QObject *parent) : QObject(parent) {}
int ImageCache::capacityMiB() const {
  return int((image::service_statistics().cache_limit / usize(1024 * 1024))
                 .to_primitive());
}
void ImageCache::setCapacityMiB(int value) {
  value = qBound(0, value, image::maximum_cache_mib);
  if (value == capacityMiB())
    return;
  image::set_cache_limit(usize(value) * usize(1024 * 1024));
  emit capacityMiBChanged();
}

static qreal now() { return image::playback_time(); }

class ImageClock final : public QObject {
  QTimer timer;
  QHash<Image *, qreal> deadlines;
  void arm() {
    if (deadlines.isEmpty()) {
      timer.stop();
      return;
    }
    qreal earliest = 1e30;
    for (auto deadline : deadlines)
      earliest = qMin(earliest, deadline);
    timer.start(int(qBound<qreal>(0, qCeil(earliest - now()), 2147483647)));
  }

public:
  explicit ImageClock(QQuickWindow *window) : QObject(window) {
    setObjectName(QStringLiteral("_qextra_image_clock"));
    timer.setSingleShot(true);
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, [this] {
      QPointer<ImageClock> guard(this);
      QList<QPointer<Image>> ready;
      const auto time = now();
      for (auto it = deadlines.begin(); it != deadlines.end();) {
        if (it.value() <= time) {
          ready.push_back(it.key());
          it = deadlines.erase(it);
        } else {
          ++it;
        }
      }
      for (auto item : ready) {
        if (item)
          QMetaObject::invokeMethod(item, "imageReady", Qt::DirectConnection);
        if (!guard)
          return;
      }
      arm();
    });
  }
  void remove(Image *item) {
    deadlines.remove(item);
    arm();
  }
  void schedule(Image *item, qreal deadline) {
    deadlines.insert(item, deadline);
    arm();
  }
};

class TextureCleanup final : public QRunnable {
  ImageProvider *provider_;

public:
  explicit TextureCleanup(ImageProvider *provider) : provider_(provider) {}
  void run() override { delete provider_; }
};

class ImagePrivate {
public:
  Image *item;
  QPointer<ImageClock> clock;
  QUrl source;
  QUrl networkUrl;
  image::NetworkLoad *download{};
  Option<wi::Source> networkInput;
  QString networkIdentity;
  qreal progress{};
  QSize requested;
  QSize maximum;
  QSize pixels;
  Image::Status status{Image::Null};
  Image::FillMode fill{Image::Stretch};
  QString error;
  Qt::Alignment horizontal{Qt::AlignHCenter};
  Qt::Alignment vertical{Qt::AlignVCenter};
  bool mirror{}, mipmap{}, retain{}, animated{}, playing{true}, paused{},
      cache{true};
  quint64 contextId{}, generation{};
  bool awaiting{}, rewind{}, needsFrame{}, suspended{}, ended{};
  int frame{}, frameCount{}, seekFrame{}, loops{-1}, iteration{};
  qreal speed{1}, deadline{}, remaining{}, dpr{1};
  Option<qreal> resumeRemaining;
  Option<image::Request> request;
  Option<image::Playback> playback;
  bool sharedPlayback{}, independentControl{};
  Option<Arc<image::Frame>> candidate;
  QImage pending;
  ImageProvider *provider{};
  QPointer<QQuickWindow> renderWindow;
  QMetaObject::Connection invalidationConnection;
  QMetaObject::Connection visibilityConnection;
  QMetaObject::Connection animationConnection;
  QList<QMetaObject::Connection> viewportConnections;
  bool inViewport{}, wakePending{};
  rstd::sync::atomic::Atomic<bool> invalidated;

  explicit ImagePrivate(Image *owner) : item(owner) {}
  ~ImagePrivate() {
    delete download;
    for (auto connection : viewportConnections)
      QObject::disconnect(connection);
    if (clock)
      clock->remove(item);
  }
  auto position() const -> qreal { return now(); }
  void wake() {
    if (wakePending)
      return;
    wakePending = true;
    QMetaObject::invokeMethod(
        item,
        [this] {
          wakePending = false;
          tick();
        },
        Qt::QueuedConnection);
  }
  void stopClock() {
    if (clock)
      clock->remove(item);
  }
  void refreshViewport() {
    QRectF area;
    if (auto *window = item->window()) {
      auto *root = window->contentItem();
      area = item->mapRectToItem(root, item->boundingRect())
                 .intersected(root->boundingRect());
      for (auto *parent = item->parentItem(); parent && !area.isEmpty();
           parent = parent->parentItem())
        if (parent->clip())
          area =
              area.intersected(parent->mapRectToItem(root, parent->clipRect()));
    }
    const bool visible = !area.isEmpty();
    if (inViewport != visible) {
      inViewport = visible;
      wake();
    }
  }
  void observeViewport() {
    for (auto connection : viewportConnections)
      QObject::disconnect(connection);
    viewportConnections.clear();
    for (auto *parent = static_cast<QQuickItem *>(item); parent;
         parent = parent->parentItem()) {
      auto observe = [this, parent](auto signal) {
        viewportConnections.push_back(QObject::connect(
            parent, signal, item, [this] { refreshViewport(); }));
      };
      observe(&QQuickItem::xChanged);
      observe(&QQuickItem::yChanged);
      observe(&QQuickItem::widthChanged);
      observe(&QQuickItem::heightChanged);
      observe(&QQuickItem::rotationChanged);
      observe(&QQuickItem::scaleChanged);
      observe(&QQuickItem::transformOriginChanged);
      observe(&QQuickItem::clipChanged);
      viewportConnections.push_back(
          QObject::connect(parent, &QQuickItem::parentChanged, item,
                           [this] { observeViewport(); }));
    }
    refreshViewport();
  }
  void state(Image::Status value, QString message = {}) {
    if (status == value && error == message)
      return;
    status = value;
    error = message;
    if (value != Image::Loading)
      setProgress(value == Image::Ready ? 1 : 0);
    emit item->statusChanged();
  }
  void setProgress(qreal value) {
    if (value == progress)
      return;
    progress = value;
    emit item->progressChanged();
  }
  void fail(QString message) {
    delete download;
    download = nullptr;
    networkUrl = QUrl();
    networkInput = None();
    networkIdentity.clear();
    request = None();
    playback = None();
    candidate = None();
    pending = {};
    stopClock();
    state(Image::Error, message);
    qmlWarning(item) << message;
    item->update();
  }
  bool active() const {
    return inViewport && item->isVisible() && item->window() &&
           item->window()->isVisible() &&
           item->window()->visibility() != QWindow::Minimized;
  }
  bool wantsShared() const {
    return animated && sharedPlayback && !independentControl && playing &&
           !paused && speed == 1 && loops == -1 && qmlEngine(item);
  }
  bool detachShared() {
    if (item->isComponentComplete())
      independentControl = true;
    if (!playback)
      return false;
    auto current = playback->snapshot(false);
    if (current.frame) {
      seekFrame = int((*current.frame)->image.index.to_primitive());
      remaining = current.remaining;
      deadline = position() + remaining;
      resumeRemaining = Some(remaining);
    }
    playback = None();
    return true;
  }
  void reload(bool keepImage = false) {
    ++generation;
    delete download;
    download = nullptr;
    networkUrl = QUrl();
    setProgress(0);
    stopClock();
    request = None();
    playback = None();
    candidate = None();
    pending = {};
    awaiting = false;
    rewind = false;
    ended = false;
    needsFrame = true;
    iteration = 0;
    deadline = position();
    if (!retain && !keepImage) {
      pixels = {};
      item->setImplicitSize(0, 0);
    }
    item->update();
    if (source.isEmpty()) {
      pixels = {};
      item->setImplicitSize(0, 0);
      state(Image::Null);
      stopClock();
      return;
    }
    if (!item->isComponentComplete())
      return;
    auto url = source;
    if (auto *context = qmlContext(item))
      url = context->resolvedUrl(url);
    QString path;
    if (url.scheme() == QLatin1String("qrc"))
      path = QLatin1String(":") + url.path();
    else if (url.isLocalFile())
      path = url.toLocalFile();
    else if (url.isRelative())
      path = url.toString();
    else if (url.scheme() == QLatin1String("http") ||
             url.scheme() == QLatin1String("https")) {
      networkUrl = url;
      path = url.path();
    } else {
      fail(QStringLiteral("Unsupported image URL scheme"));
      return;
    }
    dpr = 1;
    const auto suffix = path.lastIndexOf(QLatin1Char('.'));
    if (suffix > 3 && path.at(suffix - 1) == QLatin1Char('x') &&
        path.at(suffix - 3) == QLatin1Char('@')) {
      auto scale = path.at(suffix - 2).digitValue();
      if (scale > 0)
        dpr = scale;
    }
    if (!contextId) {
      static rstd::sync::atomic::Atomic<u64> nextContext{u64(1)};
      auto *engine = qmlEngine(item);
      auto value =
          engine ? engine->property("_qextra_image_context").toULongLong() : 0;
      if (!value) {
        value =
            nextContext.fetch_add(u64(1), rstd::sync::atomic::Ordering::Relaxed)
                .to_primitive();
        if (engine)
          engine->setProperty("_qextra_image_context",
                              QVariant::fromValue(value));
      }
      contextId = value;
    }
    state(Image::Loading);
    if (!networkUrl.isEmpty()) {
      if (networkInput) {
        networkUrl = QUrl();
        start(image::Request::open_source(
            networkIdentity, networkInput->clone(), decodeSize(), animated,
            cache, contextId, seekFrame));
        return;
      }
      wake();
      return;
    }
    start(image::Request::open(path, decodeSize(), animated, cache, contextId,
                               seekFrame));
  }
  wi::DecodeRequest decodeSize() const {
    return {u32(qMax(0, requested.width())), u32(qMax(0, requested.height())),
            fill == Image::PreserveAspectCrop ? wi::ResizeMode::Cover
                                              : wi::ResizeMode::Fit,
            u32(qMax(0, maximum.width())), u32(qMax(0, maximum.height()))};
  }
  void start(Result<image::Request, wi::Error> opened) {
    if (opened.is_err()) {
      fail(QStringLiteral("Image request limit reached"));
      return;
    }
    if (wantsShared()) {
      auto shared = image::Playback::open(
          qmlEngine(item), opened.unwrap_unchecked(), item, active());
      if (shared.is_err()) {
        fail(QStringLiteral("Cannot join shared image playback"));
        return;
      }
      playback = Some(shared.unwrap_unchecked());
      if (networkInput)
        networkInput = playback->owned_source();
    } else {
      request = Some(opened.unwrap_unchecked());
      request->observe(item, "imageReady");
    }
    state(Image::Loading);
    wake();
  }
  void publish(Arc<image::Frame> frameOwner,
               Option<qreal> sharedRemaining = {}) {
    QPointer<Image> guard(item);
    const auto before = generation;
    auto valid = [&] { return guard && guard->d->generation == before; };
    const auto oldSourceSize = item->sourceSize();
    const auto &value = frameOwner->image;
    const auto *bytes = value.pixels.data();
    auto size = QSize(int(value.width.to_primitive()),
                      int(value.height.to_primitive()));
    auto index = int(value.index.to_primitive());
    auto duration = qreal(value.duration_us.to_primitive()) / 1000.0 / speed;
    auto stride = qsizetype(value.stride.to_primitive());
    auto raw = frameOwner.into_raw();
    pending = QImage(
        bytes, size.width(), size.height(), stride,
        QImage::Format_RGBA8888_Premultiplied,
        [](void *data) {
          auto owner = Arc<image::Frame>::from_raw(
              rstd::sync::ArcRaw<image::Frame>::from_raw(data));
        },
        raw.into_raw());
    pending.setDevicePixelRatio(dpr);
    pixels = size;
    deadline = (needsFrame || position() > deadline + duration ? position()
                                                               : deadline) +
               duration;
    if (resumeRemaining.is_some())
      deadline = position() + resumeRemaining.take().unwrap_unchecked();
    if (sharedRemaining)
      deadline = position() + *sharedRemaining;
    needsFrame = false;
    if (item->sourceSize() != oldSourceSize) {
      emit item->sourceSizeChanged();
      if (!valid())
        return;
    }
    item->setImplicitSize(size.width() / dpr, size.height() / dpr);
    if (!valid())
      return;
    if (frame != index) {
      frame = index;
      emit item->currentFrameChanged();
      if (!valid())
        return;
    }
    state(Image::Ready);
    if (!valid())
      return;
    emit item->paintedGeometryChanged();
    if (!valid())
      return;
    item->update();
    if (!animated) {
      request = None();
      stopClock();
    }
  }
  void tick() {
    stopClock();
    if (invalidated.exchange(false, rstd::sync::atomic::Ordering::AcqRel)) {
      if (playback) {
        playback->refresh();
      } else if (!needsFrame) {
        seekFrame = wantsShared() ? 0 : frame;
        reload();
      }
    }
    if (playback)
      playback->suspend(!active());
    if (!active()) {
      if (!suspended) {
        if (!paused)
          remaining = qMax<qreal>(0, deadline - position());
        suspended = true;
      }
      if (request)
        request->suspend(true);
      return;
    }
    if (suspended) {
      suspended = false;
      deadline = position() + remaining;
    }
    if (!networkUrl.isEmpty()) {
      if (!download) {
        auto *engine = qmlEngine(item);
        if (!engine) {
          fail(QStringLiteral("Network images require a QML engine"));
          return;
        }
        download = new image::NetworkLoad(engine, networkUrl, item);
      }
      setProgress(download->progress());
      if (!download->done())
        return;
      const auto error = download->error();
      if (!error.isEmpty()) {
        fail(error);
        return;
      }
      auto identity = download->identity();
      auto input = download->take_source();
      delete download;
      download = nullptr;
      networkUrl = QUrl();
      if (input.is_err()) {
        fail(QStringLiteral("Cannot read downloaded image"));
        return;
      }
      networkIdentity = rstd::move(identity);
      networkInput = Some(input.unwrap_unchecked());
      start(image::Request::open_source(networkIdentity, networkInput->clone(),
                                        decodeSize(), animated, cache,
                                        contextId, seekFrame));
    }
    if (playback) {
      auto result = playback->snapshot();
      if (result.error) {
        fail(QStringLiteral("Shared image playback failed (%1)")
                 .arg(int(*result.error)));
        return;
      }
      QPointer<Image> guard(item);
      const auto before = generation;
      if (result.info.frame_count != u64() &&
          frameCount != int(result.info.frame_count.to_primitive())) {
        frameCount = int(result.info.frame_count.to_primitive());
        emit item->frameCountChanged();
        if (!guard || generation != before)
          return;
      }
      if (result.frame)
        publish(result.frame.unwrap_unchecked(), Some(result.remaining));
      return;
    }
    if (!request)
      return;
    request->suspend(paused && !needsFrame);
    if (awaiting) {
      auto result = request->poll();
      if (result.error.is_some()) {
        fail(QStringLiteral("Image decoding failed (%1)")
                 .arg(int(*result.error)));
        return;
      }
      if (result.frame.is_some()) {
        candidate = result.frame.take();
        awaiting = false;
      }
      if (result.info.frame_count != u64() &&
          frameCount != int(result.info.frame_count.to_primitive())) {
        frameCount = int(result.info.frame_count.to_primitive());
        emit item->frameCountChanged();
      }
      if (result.eof) {
        awaiting = false;
        if (needsFrame) {
          fail(QStringLiteral("Image frame index is unavailable"));
          return;
        }
        ended = true;
      }
    }
    bool running = animated && playing && !paused;
    if (ended) {
      if (!running)
        return;
      if (position() < deadline) {
        if (clock)
          clock->schedule(item, deadline);
        return;
      }
      ended = false;
      if (!animated || frameCount <= 1 ||
          (loops >= 0 && ++iteration >= loops)) {
        request = None();
        stopClock();
        if (animated && playing) {
          playing = false;
          auto *animation = static_cast<AnimatedImage *>(item);
          emit animation->playingChanged();
          emit animation->finished();
        }
        return;
      }
      rewind = true;
    }
    if (candidate.is_some() &&
        (needsFrame || (running && position() >= deadline))) {
      QPointer<Image> guard(item);
      const auto before = generation;
      publish(candidate.take().unwrap_unchecked());
      if (!guard || generation != before)
        return;
    }
    if (request && !awaiting && candidate.is_none() &&
        (needsFrame || running)) {
      awaiting = request->advance(rewind, seekFrame, qint64(deadline));
      if (awaiting) {
        rewind = false;
        seekFrame = 0;
      }
    }
    if (candidate && running && clock)
      clock->schedule(item, deadline);
  }
  QRectF painted() const {
    const qreal w = item->width(), h = item->height();
    if (pixels.isEmpty())
      return {};
    QSizeF size = QSizeF(pixels) / dpr;
    if (fill == Image::Stretch || fill == Image::Tile)
      size = QSizeF(w, h);
    else if (fill == Image::PreserveAspectFit)
      size.scale(w, h, Qt::KeepAspectRatio);
    else if (fill == Image::PreserveAspectCrop)
      size.scale(w, h, Qt::KeepAspectRatioByExpanding);
    else if (fill == Image::TileHorizontally)
      size = QSizeF(w, h);
    else if (fill == Image::TileVertically)
      size = QSizeF(w, h);
    qreal x = horizontal.testFlag(Qt::AlignLeft)    ? 0
              : horizontal.testFlag(Qt::AlignRight) ? w - size.width()
                                                    : (w - size.width()) / 2;
    qreal y = vertical.testFlag(Qt::AlignTop)      ? 0
              : vertical.testFlag(Qt::AlignBottom) ? h - size.height()
                                                   : (h - size.height()) / 2;
    return {QPointF(x, y), size};
  }
};

Image::Image(QQuickItem *parent)
    : QQuickItem(parent),
      d(rstd::move(Box<ImagePrivate>::make(this)).into_raw().as_raw_ptr()) {
  setFlag(ItemHasContents);
  connect(this, &QQuickItem::smoothChanged, this, &QQuickItem::update);
  connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
    disconnect(d->invalidationConnection);
    disconnect(d->visibilityConnection);
    disconnect(d->animationConnection);
    d->stopClock();
    releaseResources();
    d->renderWindow = window;
    if (!window) {
      d->observeViewport();
      d->wake();
      return;
    }
    auto *clock = window->findChild<QObject *>(
        QStringLiteral("_qextra_image_clock"), Qt::FindDirectChildrenOnly);
    d->clock =
        clock ? static_cast<ImageClock *>(clock) : new ImageClock(window);
    d->invalidationConnection = connect(
        window, &QQuickWindow::sceneGraphInvalidated, this,
        [this] {
          delete d->provider;
          d->provider = nullptr;
          d->invalidated.store(true, rstd::sync::atomic::Ordering::Release);
          QMetaObject::invokeMethod(
              this, [this] { d->wake(); }, Qt::QueuedConnection);
        },
        Qt::DirectConnection);
    d->visibilityConnection = connect(window, &QWindow::visibilityChanged, this,
                                      [this] { d->wake(); });
    d->animationConnection = connect(window, &QQuickWindow::afterAnimating,
                                     this, [this] { d->refreshViewport(); });
    d->observeViewport();
    if (!d->source.isEmpty())
      d->wake();
  });
}
Image::~Image() {
  // QQuickItem destruction can emit windowChanged after our private data is
  // gone.
  disconnect(this, nullptr, this, nullptr);
  disconnect(d->invalidationConnection);
  disconnect(d->visibilityConnection);
  disconnect(d->animationConnection);
  releaseResources();
  auto owner = Box<ImagePrivate>::from_raw(
      rstd::mut_ptr<ImagePrivate>::from_raw_parts(d));
}
void Image::componentComplete() {
  QQuickItem::componentComplete();
  d->observeViewport();
  d->reload();
}
void Image::geometryChange(const QRectF &rect, const QRectF &old) {
  QQuickItem::geometryChange(rect, old);
  emit paintedGeometryChanged();
  update();
}
void Image::itemChange(ItemChange change, const ItemChangeData &value) {
  QQuickItem::itemChange(change, value);
  if (change == ItemVisibleHasChanged && !d->source.isEmpty())
    d->wake();
}
void Image::imageReady() { d->tick(); }
void Image::releaseResources() {
  if (d->provider && d->renderWindow)
    d->renderWindow->scheduleRenderJob(new TextureCleanup(d->provider),
                                       QQuickWindow::AfterSynchronizingStage);
  d->provider = nullptr;
  d->invalidated.store(true, rstd::sync::atomic::Ordering::Release);
}
bool Image::isTextureProvider() const { return true; }
QSGTextureProvider *Image::textureProvider() const {
  if (QQuickItem::isTextureProvider())
    return QQuickItem::textureProvider();
  if (!window() || !window()->isSceneGraphInitialized())
    return nullptr;
  if (!window()->rhi() || QThread::currentThread() != window()->rhi()->thread())
    return nullptr;
  if (!d->provider) {
    d->provider = new ImageProvider;
    d->provider->value = new ImageTexture(window());
  }
  return d->provider;
}
QSGNode *Image::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
  auto *node = static_cast<QSGImageNode *>(old);
  if (d->pixels.isEmpty() || (d->status == Error && !d->retain)) {
    delete node;
    if (d->provider) {
      delete d->provider->value;
      d->provider->value = nullptr;
      emit d->provider->textureChanged();
    }
    return nullptr;
  }
  if (window()->rendererInterface()->graphicsApi() ==
      QSGRendererInterface::Software) {
    QMetaObject::invokeMethod(
        this,
        [this, generation = d->generation] {
          if (generation != d->generation)
            return;
          d->fail(QStringLiteral("Image requires an RHI scene graph backend"));
        },
        Qt::QueuedConnection);
    delete node;
    return nullptr;
  }
  if (!d->provider)
    d->provider = new ImageProvider;
  auto *provider = d->provider;
  if (!provider->value)
    provider->value = new ImageTexture(window());
  if (!d->pending.isNull()) {
    if (!provider->value->setImage(d->pending, d->mipmap, window()->rhi())) {
      QMetaObject::invokeMethod(
          this,
          [this, generation = d->generation] {
            if (generation != d->generation)
              return;
            d->fail(QStringLiteral(
                "Image texture allocation failed or size is unsupported"));
          },
          Qt::QueuedConnection);
      delete node;
      return nullptr;
    }
    d->pending = {};
    emit provider->textureChanged();
  }
  if (!node) {
    node = window()->createImageNode();
    node->setOwnsTexture(false);
  }
  node->setTexture(provider->value);
  auto painted = d->painted();
  auto visible = painted.intersected(boundingRect());
  QRectF sourceRect(0, 0, d->pixels.width(), d->pixels.height());
  bool tileX = d->fill == Tile || d->fill == TileHorizontally;
  bool tileY = d->fill == Tile || d->fill == TileVertically;
  provider->value->setHorizontalWrapMode(tileX ? QSGTexture::Repeat
                                               : QSGTexture::ClampToEdge);
  provider->value->setVerticalWrapMode(tileY ? QSGTexture::Repeat
                                             : QSGTexture::ClampToEdge);
  if (!painted.isEmpty()) {
    qreal sx = tileX ? d->dpr : d->pixels.width() / painted.width();
    qreal sy = tileY ? d->dpr : d->pixels.height() / painted.height();
    sourceRect = QRectF((visible.x() - painted.x()) * sx,
                        (visible.y() - painted.y()) * sy, visible.width() * sx,
                        visible.height() * sy);
  }
  node->setRect(visible);
  node->setSourceRect(sourceRect);
  node->setFiltering(smooth() ? QSGTexture::Linear : QSGTexture::Nearest);
  node->setMipmapFiltering(d->mipmap ? QSGTexture::Linear : QSGTexture::None);
  node->setTextureCoordinatesTransform(
      d->mirror ? QSGImageNode::MirrorHorizontally : QSGImageNode::NoTransform);
  return node;
}
QUrl Image::source() const { return d->source; }
void Image::setSource(const QUrl &value) {
  if (d->source == value)
    return;
  d->source = value;
  if (isComponentComplete())
    d->independentControl = false;
  d->networkInput = None();
  d->networkIdentity.clear();
  d->resumeRemaining = None();
  if (isComponentComplete())
    d->seekFrame = 0;
  d->frameCount = 0;
  emit sourceChanged();
  emit frameCountChanged();
  d->reload();
}
Image::Status Image::status() const { return d->status; }
qreal Image::progress() const { return d->progress; }
QString Image::errorString() const { return d->error; }
QSize Image::sourceSize() const {
  return {d->requested.width() != -1 ? d->requested.width()
                                     : qMax(0, d->pixels.width()),
          d->requested.height() != -1 ? d->requested.height()
                                      : qMax(0, d->pixels.height())};
}
void Image::setSourceSize(QSize value) {
  if (value == d->requested)
    return;
  d->requested = value;
  emit sourceSizeChanged();
  d->reload();
}
QSize Image::maxSize() const { return d->maximum; }
void Image::setMaxSize(QSize value) {
  if (value == d->maximum)
    return;
  d->maximum = value;
  emit maxSizeChanged();
  d->reload();
}
Image::FillMode Image::fillMode() const { return d->fill; }
void Image::setFillMode(FillMode value) {
  if (value == d->fill)
    return;
  d->fill = value;
  emit fillModeChanged();
  emit paintedGeometryChanged();
  if (d->requested.width() > 0 || d->requested.height() > 0)
    d->reload(true);
  update();
}
qreal Image::paintedWidth() const { return d->painted().width(); }
qreal Image::paintedHeight() const { return d->painted().height(); }
Qt::Alignment Image::horizontalAlignment() const { return d->horizontal; }
void Image::setHorizontalAlignment(Qt::Alignment value) {
  if (value == d->horizontal)
    return;
  d->horizontal = value;
  emit alignmentChanged();
  update();
}
Qt::Alignment Image::verticalAlignment() const { return d->vertical; }
void Image::setVerticalAlignment(Qt::Alignment value) {
  if (value == d->vertical)
    return;
  d->vertical = value;
  emit alignmentChanged();
  update();
}
bool Image::mirror() const { return d->mirror; }
void Image::setMirror(bool value) {
  if (value == d->mirror)
    return;
  d->mirror = value;
  emit mirrorChanged();
  update();
}
bool Image::mipmap() const { return d->mipmap; }
void Image::setMipmap(bool value) {
  if (value == d->mipmap)
    return;
  d->mipmap = value;
  emit mipmapChanged();
  if (d->playback) {
    d->playback->refresh();
    d->wake();
  } else {
    d->reload();
  }
}
bool Image::retainWhileLoading() const { return d->retain; }
void Image::setRetainWhileLoading(bool value) {
  if (value == d->retain)
    return;
  d->retain = value;
  emit retainWhileLoadingChanged();
}
bool Image::cache() const { return d->cache; }
void Image::setCache(bool value) {
  if (value == d->cache)
    return;
  d->cache = value;
  emit cacheChanged();
  d->reload();
}
int Image::currentFrame() const { return d->frame; }
void Image::setCurrentFrame(int value) {
  if (value < 0 || (value == d->frame && d->status == Ready))
    return;
  d->detachShared();
  d->independentControl = true;
  d->seekFrame = value;
  d->resumeRemaining = None();
  d->reload();
}
int Image::frameCount() const { return d->frameCount; }
AnimatedImage::AnimatedImage(QQuickItem *parent) : Image(parent) {
  d->animated = true;
}
bool AnimatedImage::playing() const { return d->playing; }
void AnimatedImage::setPlaying(bool value) {
  if (value == d->playing)
    return;
  const bool detached = d->detachShared();
  d->playing = value;
  emit playingChanged();
  if (!detached && isComponentComplete()) {
    d->resumeRemaining = None();
    d->seekFrame = 0;
  }
  d->reload(true);
}
bool AnimatedImage::paused() const { return d->paused; }
void AnimatedImage::setPaused(bool value) {
  if (value == d->paused)
    return;
  const bool detached = d->detachShared();
  if (value) {
    d->remaining = qMax<qreal>(0, d->deadline - d->position());
    d->stopClock();
    if (d->request && !d->needsFrame)
      d->request->suspend(true);
  } else if (d->status == Ready) {
    d->deadline = d->position() + d->remaining;
  }
  d->paused = value;
  if (detached)
    d->reload(true);
  else
    d->wake();
  emit pausedChanged();
}
qreal AnimatedImage::speed() const { return d->speed; }
void AnimatedImage::setSpeed(qreal value) {
  if (!qIsFinite(value) || value <= 0 || value == d->speed)
    return;
  const bool detached = d->detachShared();
  d->remaining =
      (d->paused ? d->remaining : qMax<qreal>(0, d->deadline - d->position())) *
      d->speed / value;
  d->deadline = d->position() + d->remaining;
  d->speed = value;
  if (detached) {
    d->resumeRemaining = Some(d->remaining);
    d->reload(true);
  } else
    d->wake();
  emit speedChanged();
}
int AnimatedImage::loops() const { return d->loops; }
void AnimatedImage::setLoops(int value) {
  if (value < -1 || value == d->loops)
    return;
  const bool detached = d->detachShared();
  d->loops = value;
  if (detached)
    d->reload(true);
  emit loopsChanged();
}
void AnimatedImage::restart() {
  d->detachShared();
  if (!d->playing) {
    d->playing = true;
    emit playingChanged();
  }
  d->seekFrame = 0;
  d->resumeRemaining = None();
  d->reload();
}
bool AnimatedImage::sharedPlayback() const { return d->sharedPlayback; }
void AnimatedImage::setSharedPlayback(bool value) {
  if (d->sharedPlayback == value)
    return;
  if (!isComponentComplete()) {
    d->sharedPlayback = value;
    emit sharedPlaybackChanged();
    return;
  }
  const bool detached = value ? false : d->detachShared();
  d->sharedPlayback = value;
  if (value) {
    d->independentControl = false;
    d->seekFrame = 0;
    d->resumeRemaining = None();
  }
  if (value || detached)
    d->reload(true);
  emit sharedPlaybackChanged();
}
} // namespace qextra
