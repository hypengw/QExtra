module;
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QtQuick/QSGTextureMaterial>
export module qextra:image.texture;
import qextra.qt;
import rstd;
using namespace rstd::prelude;

export namespace qextra::image {
struct TextureStatistics {
  quint64 allocations{}, uploads{}, hits{}, evictions{}, generation{1};
  quint64 active_bytes{}, idle_bytes{}, pending_release_bytes{}, limit{};
};
} // namespace qextra::image

namespace qextra {
struct TextureEntry {
  struct Observer {
    QPointer<QObject> receiver;
    quint64 generation;
  };
  QString key;
  QRhiTexture *texture{};
  QHash<const void *, Observer> observers;
  QImage pending, frame;
  QSize size;
  quint64 serial{}, bytes{}, stamp{}, generation{};
  int users{};
  bool mipmap{}, retained{}, dynamic{}, failed{};
};
struct TextureState {
  QQuickWindow *window;
  QMutex mutex;
  image::TextureStatistics snapshot;
  quint64 limit{}, clock{};
  bool trimming{};
  QRhi *rhi{};
  QHash<QString, QSharedPointer<TextureEntry>> entries;
  QList<QSharedPointer<TextureEntry>> retired;
  image::TextureStatistics counts;

  explicit TextureState(QQuickWindow *owner) : window(owner) {}
  ~TextureState() { Q_ASSERT(entries.isEmpty() && retired.isEmpty()); }
  void publish() {
    counts.active_bytes = counts.idle_bytes = counts.pending_release_bytes = 0;
    for (const auto &entry : entries)
      if (entry->texture)
        (entry->users ? counts.active_bytes : counts.idle_bytes) +=
            entry->bytes;
    for (const auto &entry : retired)
      counts.pending_release_bytes += entry->bytes;
    QMutexLocker lock(&mutex);
    counts.limit = limit;
    snapshot = counts;
  }
  void retire(QSharedPointer<TextureEntry> entry) {
    if (entries.value(entry->key) == entry)
      entries.remove(entry->key);
    if (entry->texture) {
      entry->texture->deleteLater();
      entry->texture = nullptr;
      ++counts.evictions;
      if (rhi && rhi->isRecordingFrame()) {
        retired.append(entry);
        return;
      }
    }
    entry->pending = {};
    entry->frame = {};
  }
  void trim() {
    quint64 capacity;
    bool all;
    {
      QMutexLocker lock(&mutex);
      capacity = limit;
      all = trimming;
      trimming = false;
    }
    quint64 idle{};
    for (const auto &entry : entries)
      if (!entry->users)
        idle += entry->bytes;
    while (all || idle > capacity) {
      QSharedPointer<TextureEntry> oldest;
      for (const auto &entry : entries)
        if (!entry->users && (!oldest || entry->stamp < oldest->stamp))
          oldest = entry;
      if (!oldest)
        break;
      idle -= oldest->bytes;
      retire(oldest);
    }
    publish();
  }
  void release(const QSharedPointer<TextureEntry> &entry,
               const void *consumer) {
    if (!entry)
      return;
    entry->observers.remove(consumer);
    if (--entry->users || entry->generation != counts.generation)
      return;
    entry->stamp = ++clock;
    if (!entry->texture || !entry->retained || !entry->pending.isNull())
      retire(entry);
    trim();
  }
  void releaseFrameInputs() {
    for (const auto &entry : entries)
      entry->frame = {};
    for (const auto &entry : retired) {
      entry->pending = {};
      entry->frame = {};
    }
    retired.clear();
  }
  void frameEnd() {
    releaseFrameInputs();
    trim();
  }
  void invalidate() {
    for (const auto &entry : entries) {
      delete entry->texture;
      entry->texture = nullptr;
      entry->pending = {};
      entry->frame = {};
    }
    entries.clear();
    releaseFrameInputs();
    rhi = nullptr;
    ++counts.generation;
    publish();
  }
  auto acquire(QImage image, bool mipmap, QRhi *backend, quint64 displayId,
               bool retained) -> QSharedPointer<TextureEntry> {
    if (!backend || image.isNull() ||
        image.width() > backend->resourceLimit(QRhi::TextureSizeMax) ||
        image.height() > backend->resourceLimit(QRhi::TextureSizeMax))
      return {};
    if (rhi && rhi != backend)
      invalidate();
    rhi = backend;
    trim();
    static rstd::sync::atomic::Atomic<u64> next{u64(1)};
    auto key = QStringLiteral("%1:%2:%3:%4")
                   .arg(displayId)
                   .arg(image.width())
                   .arg(image.height())
                   .arg(mipmap);
    if (displayId) {
      auto found = entries.value(key);
      if (found) {
        ++found->users;
        ++counts.hits;
        publish();
        return found;
      }
    }
    auto entry = QSharedPointer<TextureEntry>::create();
    entry->serial =
        next.fetch_add(u64(1), rstd::sync::atomic::Ordering::Relaxed)
            .to_primitive();
    entry->key =
        displayId ? key : QStringLiteral("dynamic:%1").arg(entry->serial);
    entry->dynamic = !displayId;
    entry->size = image.size();
    entry->mipmap = mipmap;
    entry->retained = retained && displayId;
    entry->generation = counts.generation;
    entry->pending = image;
    entry->users = 1;
    for (auto size = image.size();;) {
      entry->bytes += quint64(size.width()) * quint64(size.height()) * 4;
      if (!mipmap || size == QSize(1, 1))
        break;
      size = QSize(qMax(1, size.width() / 2), qMax(1, size.height() / 2));
    }
    entries.insert(entry->key, entry);
    publish();
    return entry;
  }
  void fail(const QSharedPointer<TextureEntry> &entry) {
    entry->failed = true;
    for (const auto &observer : entry->observers) {
      // The window outlives render callbacks; inspect consumers on its thread.
      QMetaObject::invokeMethod(
          window,
          [observer] {
            if (observer.receiver)
              QMetaObject::invokeMethod(observer.receiver, "imageTextureFailed",
                                        Qt::DirectConnection,
                                        Q_ARG(quint64, observer.generation));
          },
          Qt::QueuedConnection);
    }
    retire(entry);
    publish();
  }
  void commit(const QSharedPointer<TextureEntry> &entry, QRhi *backend,
              QRhiResourceUpdateBatch *updates) {
    if (!entry || entry->failed || entry->generation != counts.generation ||
        entry->pending.isNull())
      return;
    if (!backend || backend != rhi) {
      fail(entry);
      return;
    }
    if (!entry->texture) {
      const auto flags = entry->mipmap ? QRhiTexture::MipMapped |
                                             QRhiTexture::UsedWithGenerateMips
                                       : QRhiTexture::Flags{};
      auto *texture =
          backend->newTexture(QRhiTexture::RGBA8, entry->size, 1, flags);
      if (!texture || !texture->create()) {
        delete texture;
        fail(entry);
        return;
      }
      entry->texture = texture;
      ++counts.allocations;
    }
    entry->frame.swap(entry->pending);
    entry->pending = {};
    updates->uploadTexture(entry->texture, entry->frame);
    if (entry->mipmap)
      updates->generateMips(entry->texture);
    ++counts.uploads;
    publish();
  }
};
class TextureControl final : public QObject {
public:
  QSharedPointer<TextureState> state;
  explicit TextureControl(QQuickWindow *window)
      : QObject(window), state(QSharedPointer<TextureState>::create(window)) {
    QObject::connect(
        window, &QQuickWindow::afterFrameEnd, this,
        [state = state] { state->frameEnd(); }, Qt::DirectConnection);
    QObject::connect(
        window, &QQuickWindow::sceneGraphInvalidated, this,
        [state = state] { state->invalidate(); }, Qt::DirectConnection);
  }
};
auto texture_state(QQuickWindow *window) -> QSharedPointer<TextureState> {
  if (!window)
    return {};
  for (auto *child : window->children())
    if (auto *control = dynamic_cast<TextureControl *>(child))
      return control->state;
  Q_ASSERT(window->thread() == QThread::currentThread());
  return (new TextureControl(window))->state;
}

class ImageTexture final : public QSGTexture {
  QSharedPointer<TextureState> state_;
  QSharedPointer<TextureEntry> entry_;
  QPointer<QObject> receiver_;

public:
  ImageTexture(QSharedPointer<TextureState> state, QObject *receiver)
      : state_(state), receiver_(receiver) {}
  ~ImageTexture() override { state_->release(entry_, this); }
  bool setImage(QImage image, bool mipmap, QRhi *rhi, quint64 displayId,
                bool retained, quint64 generation) {
    if (!displayId && entry_ && !entry_->failed && entry_->dynamic &&
        rhi == state_->rhi && entry_->generation == state_->counts.generation &&
        entry_->size == image.size() && entry_->mipmap == mipmap) {
      entry_->pending = image;
      entry_->observers.insert(this, {receiver_, generation});
      return true;
    }
    auto entry = state_->acquire(image, mipmap, rhi, displayId, retained);
    if (!entry)
      return false;
    state_->release(entry_, this);
    entry_ = entry;
    entry_->observers.insert(this, {receiver_, generation});
    return true;
  }
  qint64 comparisonKey() const override {
    if (!entry_)
      return qint64(this);
    // Qt's image material compares filtering separately, but not every sampler
    // field.
    return qint64(
        (entry_->serial << 16) | (quint64(horizontalWrapMode()) << 0) |
        (quint64(verticalWrapMode()) << 2) | (quint64(mipmapFiltering()) << 4) |
        (quint64(anisotropyLevel()) << 7));
  }
  QSize textureSize() const override { return entry_ ? entry_->size : QSize(); }
  bool hasAlphaChannel() const override { return true; }
  bool hasMipmaps() const override { return entry_ && entry_->mipmap; }
  QRhiTexture *rhiTexture() const override {
    return entry_ ? entry_->texture : nullptr;
  }
  void commitTextureOperations(QRhi *rhi,
                               QRhiResourceUpdateBatch *updates) override {
    state_->commit(entry_, rhi, updates);
  }
};
void image_sampling(QSGImageNode *node, QSGTexture *texture, bool tileX,
                    bool tileY) {
  const auto h = tileX ? QSGTexture::Repeat : QSGTexture::ClampToEdge;
  const auto v = tileY ? QSGTexture::Repeat : QSGTexture::ClampToEdge;
  texture->setHorizontalWrapMode(h);
  texture->setVerticalWrapMode(v);
  for (auto *base : {node->material(), node->opaqueMaterial()})
    if (auto *material = dynamic_cast<QSGOpaqueTextureMaterial *>(base)) {
      material->setHorizontalWrapMode(h);
      material->setVerticalWrapMode(v);
    }
}
class ImageProvider final : public QSGTextureProvider {
public:
  ImageTexture *value{};
  ~ImageProvider() override { delete value; }
  QSGTexture *texture() const override { return value; }
};
} // namespace qextra

export namespace qextra::image {
bool set_texture_cache_limit(QQuickWindow *window, quint64 bytes) {
  if (!window || window->thread() != QThread::currentThread())
    return false;
  auto state = texture_state(window);
  {
    QMutexLocker lock(&state->mutex);
    state->limit = qMin<quint64>(bytes, 512ull * 1024 * 1024);
  }
  window->update();
  return true;
}
void trim_texture_cache(QQuickWindow *window) {
  if (!window || window->thread() != QThread::currentThread())
    return;
  auto state = texture_state(window);
  {
    QMutexLocker lock(&state->mutex);
    state->trimming = true;
  }
  window->update();
}
auto texture_statistics(QQuickWindow *window) -> TextureStatistics {
  if (!window || window->thread() != QThread::currentThread())
    return {};
  auto state = texture_state(window);
  QMutexLocker lock(&state->mutex);
  auto result = state->snapshot;
  result.limit = state->limit;
  return result;
}
} // namespace qextra::image
