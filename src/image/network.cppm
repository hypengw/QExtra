module;
#include <functional>
export module qextra:image.network;
import qextra.qt;
import :image.service;
import rstd;
import ncrequest;

using namespace rstd::prelude;
using namespace Qt::StringLiterals;
namespace wi = wavsen::image;
namespace ncq = ncrequest::client::qt_network;

namespace qextra::image {
constexpr auto chunk_bytes = 64 * 1024;
export struct NetworkStatistics {
  quint64 requests{}, bytes{};
  ncq::ResourceStatistics resources;
};
struct EncodedInput {
  Vec<wi::Pixels> chunks{Vec<wi::Pixels>::make()};
  usize length;
};
} // namespace qextra::image

namespace rstd {
template <>
struct Impl<io::ReadAt, qextra::image::EncodedInput>
    : DefaultInImpl<io::ReadAt, qextra::image::EncodedInput> {
  auto read_buf_at(io::BorrowedCursor target, u64 offset) const
      -> io::Result<empty> {
    const auto &input = this->self();
    if (offset >= u64(input.length.to_primitive()))
      return Ok(empty{});
    auto position = usize(offset.to_primitive());
    auto length = target.capacity().min(input.length - position);
    usize copied;
    while (copied < length) {
      auto index = position / usize(qextra::image::chunk_bytes);
      auto within = position % usize(qextra::image::chunk_bytes);
      auto count =
          (length - copied).min(usize(qextra::image::chunk_bytes) - within);
      target.append(slice<u8>::from_raw_parts(
          reinterpret_cast<const byte *>(input.chunks[index].data() +
                                         within.to_primitive()),
          count));
      copied += count;
      position += count;
    }
    return Ok(empty{});
  }
};
} // namespace rstd

namespace qextra::image {
export struct FileTarget {
  QString path;
  QString root;
};

bool same_origin(const QUrl &a, const QUrl &b) {
  return a.scheme() == b.scheme() && a.host() == b.host() &&
         a.port(a.scheme() == QLatin1String("https") ? 443 : 80) ==
             b.port(b.scheme() == QLatin1String("https") ? 443 : 80);
}

class NetworkTransfer;
export class NetworkSelection;

class NetworkQueue : public QObject {
  QQmlEngine *engine_;
  Vec<NetworkTransfer *> waiting_{Vec<NetworkTransfer *>::make()};
  Vec<NetworkTransfer *> active_{Vec<NetworkTransfer *>::make()};
  bool scheduled_{};
  bool closing_{};
  QUrl fileOrigin_;
  QString fileRoot_;
  ncq::ResourceCache *resources_{};
  NetworkStatistics statistics_;
  QHash<QUrl, QWeakPointer<NetworkTransfer>> transfers_;

public:
  void schedule();

private:
  void shutdown();

public:
  explicit NetworkQueue(QQmlEngine *engine);
  ~NetworkQueue() override;
  static NetworkQueue *get(QQmlEngine *engine);
  void enqueue(NetworkTransfer *load);
  void release(NetworkTransfer *load);
  bool configure_files(QUrl origin, QString directory);
  bool configure_cache(QList<QUrl> origins, qint64 fallback, bool all_origins);
  void observe(QObject *receiver, std::function<void(const QUrl &)> callback) {
    resources_->observe(receiver, rstd::move(callback));
  }
  void clear_cache() {
    transfers_.clear();
    resources_->clear();
  }
  auto transfer(NetworkSelection) -> QSharedPointer<NetworkTransfer>;
  void read_bytes(qint64 bytes) { statistics_.bytes += bytes; }
  auto statistics() const -> NetworkStatistics {
    auto result = statistics_;
    result.resources = resources_->statistics();
    return result;
  }
  QUrl file_origin() const { return fileOrigin_; }
  QString file_root() const { return fileRoot_; }
  auto acquire(const QUrl &url) -> ncq::ResourceRequest {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(10);
    if (!fileRoot_.isEmpty() && same_origin(url, fileOrigin_))
      request.setRawHeader("QExtra-Image-Transport", "file-v1");
    return resources_->acquire(request);
  }
  void starting() { ++statistics_.requests; }
};

export class NetworkSelection {
  friend class NetworkQueue;
  friend class NetworkTransfer;
  QUrl url_;
  ncq::ResourceRequest resource_;

public:
  NetworkSelection(QQmlEngine *engine, QUrl url)
      : url_(rstd::move(url)),
        resource_(NetworkQueue::get(engine)->acquire(url_)) {}
  NetworkSelection(NetworkSelection &&) = default;
  NetworkSelection &operator=(NetworkSelection &&) = default;
  auto snapshot() const -> ncq::ResourceSnapshot {
    return resource_.snapshot();
  }
  auto cached(wi::DecodeRequest size, bool animation, quint64 context,
              int frame) const -> Option<Request> {
    auto source = snapshot();
    if (!source.fresh())
      return None();
    return Request::cached_source(source.identity(), size, animation, context,
                                  frame, source);
  }
  auto display(wi::DecodeRequest size, quint64 context, int frame) const
      -> QSharedPointer<DisplayHandle> {
    auto source = snapshot();
    return source.fresh()
               ? Request::cached_display(source, size, context, frame)
               : QSharedPointer<DisplayHandle>{};
  }
};

class NetworkTransfer : public QObject {
  friend class NetworkQueue;
  QPointer<NetworkQueue> queue_;
  QPointer<QNetworkReply> reply_;
  struct Subscriber {
    QObject *token;
    QPointer<QObject> receiver;
    bool prefetch{};
  };
  QList<Subscriber> receivers_;
  Option<wi::Source> source_;
  EncodedInput input_;
  ncq::ResourceRequest resource_;
  struct DecodeTarget {
    wi::DecodeRequest size;
    bool animation;
    quint64 context;
    int frame;
  };
  Option<DecodeTarget> target_;
  Option<Request> prepared_;
  QUrl url_;
  QUrl fileOrigin_;
  QString fileRoot_;
  Option<FileTarget> fileTarget_;
  bool fileAllowed_{};
  bool shared_{};
  QString error_;
  bool done_{};
  bool notified_{};
  bool readPending_{};
  qint64 total_{};
  QTimer timeout_;

  void notify() {
    if (notified_)
      return;
    notified_ = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
          notified_ = false;
          for (const auto &subscriber : receivers_)
            if (subscriber.receiver)
              QMetaObject::invokeMethod(subscriber.receiver, "imageReady",
                                        Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
  }
  void stop(QString error) {
    if (done_)
      return;
    done_ = true;
    error_ = rstd::move(error);
    timeout_.stop();
    if (reply_) {
      QObject::disconnect(reply_, nullptr, this, nullptr);
      reply_->abort();
      reply_->deleteLater();
      reply_ = nullptr;
    }
    input_ = EncodedInput{};
    if (queue_)
      queue_->release(this);
    notify();
  }
  void complete_file(const QByteArray &encoded) {
    auto target = QUrl::fromEncoded(encoded, QUrl::StrictMode);
    const auto path = QDir::cleanPath(target.toLocalFile());
    if (!fileAllowed_ || !same_origin(reply_->url(), fileOrigin_) ||
        !target.isValid() || !target.isLocalFile() ||
        (!target.host().isEmpty() &&
         target.host() != QLatin1String("localhost")) ||
        !target.userInfo().isEmpty() || target.hasQuery() ||
        target.hasFragment() || !QDir::isAbsolutePath(path) ||
        !path.startsWith(fileRoot_ + QLatin1Char('/'))) {
      stop(u"Local image source is not allowed"_s);
      return;
    }
    fileTarget_ = Some(FileTarget{path, fileRoot_});
    done_ = true;
    timeout_.stop();
    QObject::disconnect(reply_, nullptr, this, nullptr);
    reply_->abort();
    reply_->deleteLater();
    reply_ = nullptr;
    input_ = EncodedInput{};
    if (queue_)
      queue_->release(this);
    notify();
  }
  void headers() {
    if (done_ || !reply_)
      return;
    const auto targets = reply_->headers().values("QExtra-Image-Source");
    if (!targets.isEmpty()) {
      if (targets.size() != 1 || targets[0].size() > 16384 ||
          reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() !=
              200) {
        stop(u"Invalid local image source response"_s);
        return;
      }
      complete_file(targets[0]);
      return;
    }
    const auto limit = wi::Limits{}.encoded_bytes;
    auto declared =
        reply_->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    if (declared > 0 && u64(declared) > limit) {
      stop(u"Image response exceeds the encoded size limit"_s);
      return;
    }
    total_ = declared;
    if (target_ && !shared_) {
      auto cached = this->cached(target_->size, target_->animation,
                                 target_->context, target_->frame);
      if (cached) {
        prepared_ = cached.take();
        done_ = true;
        timeout_.stop();
        QObject::disconnect(reply_, nullptr, this, nullptr);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
        if (queue_)
          queue_->release(this);
        notify();
      }
    }
  }
  void read() {
    headers();
    if (done_ || !reply_)
      return;
    if (reply_->isFinished() && reply_->error() != QNetworkReply::NoError) {
      stop(reply_->errorString());
      return;
    }
    const auto limit = wi::Limits{}.encoded_bytes;
    qint64 remaining = 4 * chunk_bytes;
    while (reply_ && reply_->bytesAvailable() > 0 && remaining > 0) {
      if (u64(input_.length.to_primitive()) >= limit) {
        stop(u"Image response exceeds the encoded size limit"_s);
        return;
      }
      auto within = input_.length % usize(chunk_bytes);
      if (within == usize() &&
          input_.chunks.len() == input_.length / usize(chunk_bytes)) {
        auto chunk = allocate_input(usize(chunk_bytes));
        if (chunk.is_err() || input_.chunks.try_reserve(usize(1)).is_err()) {
          stop(u"Image input memory budget exhausted"_s);
          return;
        }
        input_.chunks.push(chunk.unwrap_unchecked());
      }
      auto *data =
          reinterpret_cast<char *>(
              input_.chunks[input_.length / usize(chunk_bytes)].data_mut()) +
          within.to_primitive();
      const auto count = reply_->read(
          data, qMin(remaining, chunk_bytes - qint64(within.to_primitive())));
      if (count < 0) {
        stop(u"Cannot read image response"_s);
        return;
      }
      if (count == 0)
        break;
      input_.length += usize(count);
      if (queue_)
        queue_->read_bytes(count);
      remaining -= count;
    }
    if (remaining == 0 && reply_->bytesAvailable() > 0) {
      if (!readPending_) {
        readPending_ = true;
        QMetaObject::invokeMethod(
            this,
            [this] {
              readPending_ = false;
              read();
            },
            Qt::QueuedConnection);
      }
    } else if (reply_->isFinished()) {
      done_ = true;
      timeout_.stop();
      reply_->deleteLater();
      reply_ = nullptr;
      if (queue_)
        queue_->release(this);
    }
    notify();
  }

  void start(QQmlEngine *engine) {
    queue_->starting();
    reply_ = resource_.open();
    if (!reply_) {
      stop(u"Image network resource is no longer available"_s);
      return;
    }
    QObject::connect(
        reply_, &QNetworkReply::redirected, this, [this](const QUrl &target) {
          fileAllowed_ = fileAllowed_ && same_origin(target, fileOrigin_);
        });
    reply_->setReadBufferSize(chunk_bytes);
    QObject::connect(reply_, &QNetworkReply::readyRead, this,
                     [this] { read(); });
    // Draining on metadata can prevent Qt's cached reply from reaching
    // finished.
    QObject::connect(reply_, &QNetworkReply::metaDataChanged, this,
                     [this] { headers(); });
    QObject::connect(reply_, &QNetworkReply::finished, this,
                     [this] { read(); });
    QObject::connect(reply_, &QObject::destroyed, this, [this] {
      reply_ = nullptr;
      if (!done_)
        stop(u"Image network manager was destroyed"_s);
    });
    timeout_.start(30000);
    if (reply_->isFinished())
      QMetaObject::invokeMethod(this, [this] { read(); }, Qt::QueuedConnection);
  }

public:
  NetworkTransfer(QQmlEngine *engine, NetworkSelection selected)
      : queue_(NetworkQueue::get(engine)),
        resource_(rstd::move(selected.resource_)),
        url_(rstd::move(selected.url_)) {
    fileOrigin_ = queue_->file_origin();
    fileRoot_ = queue_->file_root();
    fileAllowed_ = !fileRoot_.isEmpty() && same_origin(url_, fileOrigin_);
    timeout_.setSingleShot(true);
    QObject::connect(&timeout_, &QTimer::timeout, this,
                     [this] { stop(u"Image download timed out"_s); });
    queue_->enqueue(this);
  }
  ~NetworkTransfer() override {
    if (reply_) {
      QObject::disconnect(reply_, nullptr, this, nullptr);
      reply_->abort();
      reply_->deleteLater();
    }
    if (queue_)
      queue_->release(this);
  }
  void subscribe(QObject *token, QObject *receiver) {
    shared_ |= !receivers_.isEmpty();
    receivers_.append({token, receiver, false});
  }
  void unsubscribe(QObject *token) {
    receivers_.removeIf(
        [token](const auto &subscriber) { return subscriber.token == token; });
    if (queue_)
      queue_->schedule();
  }
  bool prefetch() const {
    for (const auto &subscriber : receivers_)
      if (!subscriber.prefetch)
        return false;
    return true;
  }
  void set_prefetch(QObject *token, bool value) {
    for (auto &subscriber : receivers_)
      if (subscriber.token == token)
        subscriber.prefetch = value;
    if (queue_)
      queue_->schedule();
  }
  bool done() const { return done_; }
  QString error() const { return error_; }
  qreal progress() const {
    return total_ > 0
               ? qMin(qreal(1), qreal(input_.length.to_primitive()) / total_)
               : 0;
  }
  auto snapshot() const -> ncq::ResourceSnapshot {
    return resource_.snapshot();
  }
  QString identity() const { return snapshot().identity(); }
  bool reusable() const { return resource_.snapshot().reusable(); }
  void cache_target(wi::DecodeRequest size, bool animation, quint64 context,
                    int frame) {
    target_ = Some(DecodeTarget{size, animation, context, frame});
  }
  auto take_cached() -> Option<Request> { return prepared_.take(); }
  auto cached(wi::DecodeRequest size, bool animation, quint64 context,
              int frame) const -> Option<Request> {
    auto snapshot = resource_.snapshot();
    if (!snapshot.reusable())
      return None();
    return Request::cached_source(snapshot.identity(), size, animation, context,
                                  frame, snapshot);
  }
  static auto cached(QQmlEngine *engine, const QUrl &url,
                     wi::DecodeRequest size, bool animation, quint64 context,
                     int frame) -> Option<Request> {
    return NetworkSelection(engine, url)
        .cached(size, animation, context, frame);
  }
  auto take_file() -> Option<FileTarget> {
    return fileTarget_ ? Some(FileTarget{fileTarget_->path, fileTarget_->root})
                       : Option<FileTarget>{};
  }
  auto take_source() -> Result<wi::Source, wi::Error> {
    if (source_)
      return Ok(source_->clone());
    if (!done_ || !error_.isEmpty() || input_.length == usize())
      return Err(wi::Error::InvalidInput);
    const auto length = u64(input_.length.to_primitive());
    auto owner = rstd::io::SharedReadAt::try_make(rstd::move(input_));
    if (owner.is_err()) {
      error_ = u"Image input memory budget exhausted"_s;
      return Err(wi::Error::ResourceLimit);
    }
    auto range =
        rstd::io::ReadRange::make(owner.unwrap_unchecked(), u64(), length);
    if (range.is_err()) {
      error_ = u"Invalid image input range"_s;
      return Err(wi::Error::InvalidInput);
    }
    source_ = Some(wi::Source::reader(range.unwrap_unchecked()));
    return Ok(source_->clone());
  }
};

export class NetworkLoad : public QObject {
  QSharedPointer<NetworkTransfer> transfer_;

public:
  NetworkLoad(QQmlEngine *engine, QUrl url, QObject *receiver)
      : NetworkLoad(engine, NetworkSelection(engine, rstd::move(url)),
                    receiver) {}
  NetworkLoad(QQmlEngine *engine, NetworkSelection selected, QObject *receiver)
      : QObject(receiver),
        transfer_(NetworkQueue::get(engine)->transfer(rstd::move(selected))) {
    transfer_->subscribe(this, receiver);
  }
  ~NetworkLoad() override { transfer_->unsubscribe(this); }
  void set_prefetch(bool value) { transfer_->set_prefetch(this, value); }
  bool done() const { return transfer_->done(); }
  QString error() const { return transfer_->error(); }
  qreal progress() const { return transfer_->progress(); }
  auto snapshot() const -> ncq::ResourceSnapshot {
    return transfer_->snapshot();
  }
  QString identity() const { return transfer_->identity(); }
  bool reusable() const { return transfer_->reusable(); }
  void cache_target(wi::DecodeRequest size, bool animation, quint64 context,
                    int frame) {
    transfer_->cache_target(size, animation, context, frame);
  }
  auto take_cached() -> Option<Request> { return transfer_->take_cached(); }
  auto take_file() -> Option<FileTarget> { return transfer_->take_file(); }
  auto take_source() -> Result<wi::Source, wi::Error> {
    return transfer_->take_source();
  }
  static auto cached(QQmlEngine *engine, const QUrl &url,
                     wi::DecodeRequest size, bool animation, quint64 context,
                     int frame) -> Option<Request> {
    return NetworkTransfer::cached(engine, url, size, animation, context,
                                   frame);
  }
};

auto NetworkQueue::transfer(NetworkSelection selected)
    -> QSharedPointer<NetworkTransfer> {
  const auto url = selected.url_;
  auto found = transfers_.value(url).toStrongRef();
  if (found && !found->done())
    return found;
  for (auto it = transfers_.begin(); it != transfers_.end();)
    if (it.value().isNull())
      it = transfers_.erase(it);
    else
      ++it;
  auto created =
      QSharedPointer<NetworkTransfer>::create(engine_, rstd::move(selected));
  transfers_.insert(url, created.toWeakRef());
  return created;
}

NetworkQueue::NetworkQueue(QQmlEngine *engine)
    : QObject(engine), engine_(engine) {
  auto *manager = engine->networkAccessManager();
  resources_ = ncq::ResourceCache::get(manager);
  if (!resources_)
    resources_ = new ncq::ResourceCache(manager);
  QObject::connect(engine, &QObject::destroyed, this, [this] { shutdown(); });
}

NetworkQueue::~NetworkQueue() { shutdown(); }

NetworkQueue *NetworkQueue::get(QQmlEngine *engine) {
  for (auto *child : engine->children()) {
    if (auto *queue = dynamic_cast<NetworkQueue *>(child))
      return queue;
  }
  return new NetworkQueue(engine);
}

void NetworkQueue::enqueue(NetworkTransfer *load) {
  if (closing_) {
    load->stop(u"Image network engine was destroyed"_s);
    return;
  }
  waiting_.emplace_back(load);
  schedule();
}

void NetworkQueue::release(NetworkTransfer *load) {
  if (closing_)
    return;
  waiting_.retain([load](auto *entry) { return entry != load; });
  active_.retain([load](auto *entry) { return entry != load; });
  schedule();
}

void NetworkQueue::schedule() {
  if (closing_ || scheduled_ || waiting_.is_empty())
    return;
  scheduled_ = true;
  QMetaObject::invokeMethod(
      this,
      [this] {
        scheduled_ = false;
        while (!closing_ && active_.len() < usize(8) && !waiting_.is_empty()) {
          Option<usize> selected;
          for (usize i; i < waiting_.len(); ++i)
            if (!waiting_[i]->prefetch()) {
              selected = Some(i);
              break;
            }
          if (!selected) {
            if (active_.len() >= usize(6))
              break;
            selected = Some(usize());
          }
          auto *load = waiting_.remove(*selected);
          active_.emplace_back(load);
          load->start(engine_);
        }
      },
      Qt::QueuedConnection);
}

void NetworkQueue::shutdown() {
  if (closing_)
    return;
  closing_ = true;
  while (auto load = waiting_.pop())
    (*load)->stop(u"Image network engine was destroyed"_s);
  while (auto load = active_.pop())
    (*load)->stop(u"Image network engine was destroyed"_s);
}
} // namespace qextra::image

namespace qextra::image {
bool NetworkQueue::configure_files(QUrl origin, QString directory) {
  if (!origin.isEmpty() &&
      (!origin.isValid() ||
       (origin.scheme() != QLatin1String("http") &&
        origin.scheme() != QLatin1String("https")) ||
       !origin.userInfo().isEmpty() || origin.hasQuery() ||
       origin.hasFragment() ||
       (!origin.path().isEmpty() && origin.path() != QLatin1String("/")) ||
       !QDir::isAbsolutePath(directory)))
    return false;
  const auto root = origin.isEmpty() ? QString() : QDir::cleanPath(directory);
  if (origin == fileOrigin_ && root == fileRoot_)
    return true;
  fileOrigin_ = rstd::move(origin);
  fileRoot_ = root;
  transfers_.clear();
  resources_->invalidate();
  return true;
}

bool NetworkQueue::configure_cache(QList<QUrl> origins, qint64 fallback,
                                   bool all_origins) {
  if (fallback < 0)
    return false;
  for (const auto &origin : origins)
    if (!origin.isValid() ||
        (origin.scheme() != QLatin1String("http") &&
         origin.scheme() != QLatin1String("https")) ||
        !origin.userInfo().isEmpty() || origin.hasQuery() ||
        origin.hasFragment() ||
        (!origin.path().isEmpty() && origin.path() != QLatin1String("/")))
      return false;
  ncq::ResourceOptions options;
  options.origins = rstd::move(origins);
  options.fallback_seconds = fallback;
  options.all_origins = all_origins;
  options.excluded_response_headers.append("QExtra-Image-Source");
  transfers_.clear();
  resources_->configure(options);
  return true;
}

export bool set_local_file_source(QQmlEngine *engine, QUrl origin,
                                  QString directory) {
  if (!engine || engine->thread() != QThread::currentThread())
    return false;
  return NetworkQueue::get(engine)->configure_files(rstd::move(origin),
                                                    rstd::move(directory));
}

export bool set_network_cache(QQmlEngine *engine, QList<QUrl> origins,
                              qint64 fallback_seconds = 0,
                              bool all_origins = false) {
  if (!engine || engine->thread() != QThread::currentThread())
    return false;
  return NetworkQueue::get(engine)->configure_cache(
      rstd::move(origins), fallback_seconds, all_origins);
}

export void observe_network_cache(QQmlEngine *engine, QObject *receiver,
                                  std::function<void(const QUrl &)> callback) {
  NetworkQueue::get(engine)->observe(receiver, rstd::move(callback));
}

export void clear_network_cache(QQmlEngine *engine) {
  if (engine && engine->thread() == QThread::currentThread())
    NetworkQueue::get(engine)->clear_cache();
}

export auto network_statistics(QQmlEngine *engine) -> NetworkStatistics {
  return NetworkQueue::get(engine)->statistics();
}
} // namespace qextra::image
