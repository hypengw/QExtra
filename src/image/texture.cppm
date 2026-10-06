module qextra:image.texture;
import qextra.qt;

namespace qextra {

class ImageTexture final : public QSGTexture {
  QRhiTexture *texture_{};
  QImage pending_;
  QImage frame_;
  QSize size_;
  bool mipmap_{};
  bool allocatedMipmap_{};

public:
  explicit ImageTexture(QQuickWindow *window) {
    connect(
        window, &QQuickWindow::afterFrameEnd, this, [this] { frame_ = {}; },
        Qt::DirectConnection);
  }
  ~ImageTexture() override { delete texture_; }
  bool setImage(QImage image, bool mipmap, QRhi *rhi) {
    if (!rhi || image.width() > rhi->resourceLimit(QRhi::TextureSizeMax) ||
        image.height() > rhi->resourceLimit(QRhi::TextureSizeMax))
      return false;
    if (!texture_ || texture_->pixelSize() != image.size() ||
        allocatedMipmap_ != mipmap) {
      auto flags =
          mipmap ? QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips
                 : QRhiTexture::Flags{};
      auto *texture =
          rhi->newTexture(QRhiTexture::RGBA8, image.size(), 1, flags);
      if (!texture || !texture->create()) {
        delete texture;
        return false;
      }
      if (texture_)
        texture_->deleteLater();
      texture_ = texture;
      allocatedMipmap_ = mipmap;
    }
    size_ = image.size();
    pending_ = image;
    mipmap_ = mipmap;
    return true;
  }
  qint64 comparisonKey() const override { return qint64(this); }
  QSize textureSize() const override { return size_; }
  bool hasAlphaChannel() const override { return true; }
  bool hasMipmaps() const override { return mipmap_; }
  QRhiTexture *rhiTexture() const override { return texture_; }
  void commitTextureOperations(QRhi *,
                               QRhiResourceUpdateBatch *updates) override {
    if (pending_.isNull())
      return;
    // External pixels must survive endFrame, not merely resourceUpdate
    // submission.
    frame_.swap(pending_);
    updates->uploadTexture(texture_, frame_);
    if (mipmap_)
      updates->generateMips(texture_);
  }
};

class ImageProvider final : public QSGTextureProvider {
public:
  ImageTexture *value{};
  ~ImageProvider() override { delete value; }
  QSGTexture *texture() const override { return value; }
};
} // namespace qextra
