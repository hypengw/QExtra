export module qextra:image.texture;
import :image.service;
import qextra.qt;

export namespace qextra::image {
struct TextureStatistics {
  quint64 allocations{}, uploads{}, hits{}, evictions{}, generation{1};
  quint64 active_bytes{}, idle_bytes{}, pending_release_bytes{}, limit{};
};
bool set_texture_cache_limit(QQuickWindow *window, quint64 bytes);
void trim_texture_cache(QQuickWindow *window);
auto texture_statistics(QQuickWindow *window) -> TextureStatistics;
} // namespace qextra::image

namespace qextra {
struct TextureEntry;
struct TextureState;

auto texture_state(QQuickWindow *window) -> QSharedPointer<TextureState>;

class ImageTexture final : public QSGTexture {
  QSharedPointer<TextureState> state_;
  QSharedPointer<TextureEntry> entry_;
  QPointer<QObject> receiver_;

public:
  ImageTexture(QSharedPointer<TextureState> state, QObject *receiver);
  ~ImageTexture() override;
  bool setImage(QImage image, bool mipmap, QRhi *rhi, quint64 displayId,
                bool retained, quint64 generation,
                QSharedPointer<image::DisplayHandle> display);
  bool setDisplay(const QSharedPointer<image::DisplayHandle> &display,
                  bool mipmap, QRhi *rhi, quint64 generation);
  qint64 comparisonKey() const override;
  QSize textureSize() const override;
  bool hasAlphaChannel() const override;
  bool hasMipmaps() const override;
  QRhiTexture *rhiTexture() const override;
  void commitTextureOperations(QRhi *rhi,
                               QRhiResourceUpdateBatch *updates) override;
};

void image_sampling(QSGImageNode *node, QSGTexture *texture, bool tileX,
                    bool tileY);

class ImageProvider final : public QSGTextureProvider {
public:
  ImageTexture *value{};
  ~ImageProvider() override;
  QSGTexture *texture() const override;
};
} // namespace qextra
