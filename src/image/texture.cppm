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
  QString key;
  QRhiTexture *texture{};
  QImage pending, frame;
  QSize size;
  quint64 serial{}, bytes{}, stamp{}, generation{};
  int users{};
  bool mipmap{}, retained{}, dynamic{};
};
struct TextureState {
  QMutex mutex;
  image::TextureStatistics snapshot;
  quint64 limit{}, clock{};
  bool trimming{};
  QRhi *rhi{};
  QHash<QString, QSharedPointer<TextureEntry>> entries;
  QList<QSharedPointer<TextureEntry>> retired;
  image::TextureStatistics counts;

  ~TextureState() { Q_ASSERT(entries.isEmpty() && retired.isEmpty()); }
  void publish() {
    counts.active_bytes = counts.idle_bytes = counts.pending_release_bytes = 0;
    for (const auto &entry : entries)
      (entry->users ? counts.active_bytes : counts.idle_bytes) += entry->bytes;
    for (const auto &entry : retired)
      counts.pending_release_bytes += entry->bytes;
    QMutexLocker lock(&mutex);
    counts.limit = limit;
    snapshot = counts;
  }
  void retire(QSharedPointer<TextureEntry> entry) {
    entries.remove(entry->key);
    if (entry->texture) {
      entry->texture->deleteLater();
      entry->texture = nullptr;
    }
    if (rhi && rhi->isRecordingFrame())
      retired.append(entry);
    else {
      entry->pending = {};
      entry->frame = {};
    }
    ++counts.evictions;
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
  void release(const QSharedPointer<TextureEntry> &entry) {
    if (!entry || --entry->users || !entry->texture)
      return;
    entry->stamp = ++clock;
    if (!entry->retained || !entry->pending.isNull())
      retire(entry);
    trim();
  }
  void frameEnd() {
    for (const auto &entry : entries)
      entry->frame = {};
    retired.clear();
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
    retired.clear();
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
    auto flags =
        mipmap ? QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips
               : QRhiTexture::Flags{};
    entry->texture =
        backend->newTexture(QRhiTexture::RGBA8, image.size(), 1, flags);
    if (!entry->texture || !entry->texture->create()) {
      delete entry->texture;
      return {};
    }
    entry->pending = image;
    entry->users = 1;
    for (auto size = image.size();;) {
      entry->bytes += quint64(size.width()) * quint64(size.height()) * 4;
      if (!mipmap || size == QSize(1, 1))
        break;
      size = QSize(qMax(1, size.width() / 2), qMax(1, size.height() / 2));
    }
    entries.insert(entry->key, entry);
    ++counts.allocations;
    publish();
    return entry;
  }
};
class TextureControl final : public QObject {
public:
  QSharedPointer<TextureState> state{QSharedPointer<TextureState>::create()};
  explicit TextureControl(QQuickWindow *window) : QObject(window) {
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

public:
  explicit ImageTexture(QSharedPointer<TextureState> state) : state_(state) {}
  ~ImageTexture() override { state_->release(entry_); }
  bool setImage(QImage image, bool mipmap, QRhi *rhi, quint64 displayId,
                bool retained) {
    if (!displayId && entry_ && entry_->texture && entry_->dynamic &&
        entry_->size == image.size() && entry_->mipmap == mipmap) {
      entry_->pending = image;
      return true;
    }
    auto entry = state_->acquire(image, mipmap, rhi, displayId, retained);
    if (!entry)
      return false;
    state_->release(entry_);
    entry_ = entry;
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
  void commitTextureOperations(QRhi *,
                               QRhiResourceUpdateBatch *updates) override {
    if (!entry_ || !entry_->texture || entry_->pending.isNull())
      return;
    entry_->frame.swap(entry_->pending);
    entry_->pending = {};
    updates->uploadTexture(entry_->texture, entry_->frame);
    if (entry_->mipmap)
      updates->generateMips(entry_->texture);
    ++state_->counts.uploads;
    state_->publish();
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
