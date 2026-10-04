#pragma once

#include <QtCore/QUrl>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

namespace qextra {
class ImagePrivate;

class ImageCache : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(ImageCache)
  QML_SINGLETON
  Q_PROPERTY(int capacityMiB READ capacityMiB WRITE setCapacityMiB NOTIFY
                 capacityMiBChanged)
public:
  explicit ImageCache(QObject *parent = nullptr);
  int capacityMiB() const;
  void setCapacityMiB(int);
signals:
  void capacityMiBChanged();
};

class Image : public QQuickItem {
  friend class ImagePrivate;
  Q_OBJECT
  QML_NAMED_ELEMENT(Image)
  Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(Status status READ status NOTIFY statusChanged)
  Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
  Q_PROPERTY(QSize sourceSize READ sourceSize WRITE setSourceSize NOTIFY
                 sourceSizeChanged)
  Q_PROPERTY(QSize maxSize READ maxSize WRITE setMaxSize NOTIFY maxSizeChanged)
  Q_PROPERTY(
      FillMode fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
  Q_PROPERTY(qreal paintedWidth READ paintedWidth NOTIFY paintedGeometryChanged)
  Q_PROPERTY(
      qreal paintedHeight READ paintedHeight NOTIFY paintedGeometryChanged)
  Q_PROPERTY(Qt::Alignment horizontalAlignment READ horizontalAlignment WRITE
                 setHorizontalAlignment NOTIFY alignmentChanged)
  Q_PROPERTY(Qt::Alignment verticalAlignment READ verticalAlignment WRITE
                 setVerticalAlignment NOTIFY alignmentChanged)
  Q_PROPERTY(bool mirror READ mirror WRITE setMirror NOTIFY mirrorChanged)
  Q_PROPERTY(bool mipmap READ mipmap WRITE setMipmap NOTIFY mipmapChanged)
  Q_PROPERTY(bool retainWhileLoading READ retainWhileLoading WRITE
                 setRetainWhileLoading NOTIFY retainWhileLoadingChanged)
  Q_PROPERTY(bool cache READ cache WRITE setCache NOTIFY cacheChanged)
  Q_PROPERTY(int currentFrame READ currentFrame WRITE setCurrentFrame NOTIFY
                 currentFrameChanged)
  Q_PROPERTY(int frameCount READ frameCount NOTIFY frameCountChanged)
public:
  enum Status { Null, Ready, Loading, Error };
  Q_ENUM(Status)
  enum FillMode {
    Stretch,
    PreserveAspectFit,
    PreserveAspectCrop,
    Tile,
    TileVertically,
    TileHorizontally,
    Pad
  };
  Q_ENUM(FillMode)
  enum HAlignment {
    AlignLeft = Qt::AlignLeft,
    AlignRight = Qt::AlignRight,
    AlignHCenter = Qt::AlignHCenter
  };
  Q_ENUM(HAlignment)
  enum VAlignment {
    AlignTop = Qt::AlignTop,
    AlignBottom = Qt::AlignBottom,
    AlignVCenter = Qt::AlignVCenter
  };
  Q_ENUM(VAlignment)

  explicit Image(QQuickItem *parent = nullptr);
  ~Image() override;
  QUrl source() const;
  void setSource(const QUrl &);
  Status status() const;
  qreal progress() const;
  QString errorString() const;
  QSize sourceSize() const;
  void setSourceSize(QSize);
  QSize maxSize() const;
  void setMaxSize(QSize);
  FillMode fillMode() const;
  void setFillMode(FillMode);
  qreal paintedWidth() const;
  qreal paintedHeight() const;
  Qt::Alignment horizontalAlignment() const;
  void setHorizontalAlignment(Qt::Alignment);
  Qt::Alignment verticalAlignment() const;
  void setVerticalAlignment(Qt::Alignment);
  bool mirror() const;
  void setMirror(bool);
  bool mipmap() const;
  void setMipmap(bool);
  bool retainWhileLoading() const;
  void setRetainWhileLoading(bool);
  bool cache() const;
  void setCache(bool);
  int currentFrame() const;
  void setCurrentFrame(int);
  int frameCount() const;
  bool isTextureProvider() const override;
  QSGTextureProvider *textureProvider() const override;

signals:
  void sourceChanged();
  void statusChanged();
  void progressChanged();
  void sourceSizeChanged();
  void maxSizeChanged();
  void fillModeChanged();
  void paintedGeometryChanged();
  void alignmentChanged();
  void mirrorChanged();
  void mipmapChanged();
  void retainWhileLoadingChanged();
  void cacheChanged();
  void currentFrameChanged();
  void frameCountChanged();

protected:
  void componentComplete() override;
  void geometryChange(const QRectF &, const QRectF &) override;
  QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
  void releaseResources() override;
  void itemChange(ItemChange, const ItemChangeData &) override;
  ImagePrivate *d;
private slots:
  void imageReady();
};

class AnimatedImage : public Image {
  Q_OBJECT
  QML_NAMED_ELEMENT(AnimatedImage)
  Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
  Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
  Q_PROPERTY(qreal speed READ speed WRITE setSpeed NOTIFY speedChanged)
  Q_PROPERTY(int loops READ loops WRITE setLoops NOTIFY loopsChanged)
  Q_PROPERTY(bool sharedPlayback READ sharedPlayback WRITE setSharedPlayback
                 NOTIFY sharedPlaybackChanged)
public:
  explicit AnimatedImage(QQuickItem *parent = nullptr);
  bool playing() const;
  void setPlaying(bool);
  bool paused() const;
  void setPaused(bool);
  qreal speed() const;
  void setSpeed(qreal);
  int loops() const;
  void setLoops(int);
  bool sharedPlayback() const;
  void setSharedPlayback(bool);
  Q_INVOKABLE void restart();
signals:
  void playingChanged();
  void pausedChanged();
  void speedChanged();
  void loopsChanged();
  void sharedPlaybackChanged();
  void finished();
};
} // namespace qextra
