export module qextra:image.network;
import qextra.qt;
import :image.service;
import rstd;

using namespace rstd::prelude;
using namespace Qt::StringLiterals;
namespace wi = wavsen::image;

namespace qextra::image {
constexpr auto chunk_bytes = 64 * 1024;
struct EncodedInput {
  Vec<wi::Pixels> chunks{Vec<wi::Pixels>::make()};
  usize length;
};
} // namespace qextra::image

namespace rstd {
template <>
struct Impl<io::ReadAt, qextra::image::EncodedInput>
    : ImplBase<qextra::image::EncodedInput> {
  auto read_at(mut_ref<u8[]> target, u64 offset) const -> io::Result<usize> {
    const auto &input = this->self();
    if (offset >= u64(input.length.to_primitive()))
      return Ok(usize());
    auto position = usize(offset.to_primitive());
    auto length = target.len().min(input.length - position);
    usize copied;
    while (copied < length) {
      auto index = position / usize(qextra::image::chunk_bytes);
      auto within = position % usize(qextra::image::chunk_bytes);
      auto count =
          (length - copied).min(usize(qextra::image::chunk_bytes) - within);
      mem::memcpy(target.as_raw_ptr() + copied.to_primitive(),
                  input.chunks[index].data() + within.to_primitive(), count);
      copied += count;
      position += count;
    }
    return Ok(copied);
  }
};
} // namespace rstd

namespace qextra::image {
export class NetworkLoad;

class NetworkQueue : public QObject {
  QQmlEngine *engine_;
  Vec<NetworkLoad *> waiting_{Vec<NetworkLoad *>::make()};
  Vec<NetworkLoad *> active_{Vec<NetworkLoad *>::make()};
  bool scheduled_{};
  bool closing_{};

  void schedule();
  void shutdown();

public:
  explicit NetworkQueue(QQmlEngine *engine);
  ~NetworkQueue() override;
  static NetworkQueue *get(QQmlEngine *engine);
  void enqueue(NetworkLoad *load);
  void release(NetworkLoad *load);
};

export class NetworkLoad : public QObject {
  friend class NetworkQueue;
  QPointer<NetworkQueue> queue_;
  QPointer<QNetworkReply> reply_;
  QPointer<QObject> receiver_;
  EncodedInput input_;
  QCryptographicHash hash_{QCryptographicHash::Sha256};
  QUrl url_;
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
          if (receiver_)
            QMetaObject::invokeMethod(receiver_, "imageReady",
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
  void headers() {
    if (done_ || !reply_)
      return;
    const auto limit = wi::Limits{}.encoded_bytes;
    auto declared =
        reply_->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    if (declared > 0 && u64(declared) > limit) {
      stop(u"Image response exceeds the encoded size limit"_s);
      return;
    }
    total_ = declared;
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
      hash_.addData(QByteArrayView(data, count));
      input_.length += usize(count);
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
    QNetworkRequest request(url_);
    request.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, true);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(10);
    reply_ = engine->networkAccessManager()->get(request);
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
  NetworkLoad(QQmlEngine *engine, QUrl url, QObject *receiver)
      : QObject(receiver), queue_(NetworkQueue::get(engine)),
        receiver_(receiver), url_(rstd::move(url)) {
    timeout_.setSingleShot(true);
    QObject::connect(&timeout_, &QTimer::timeout, this, [this] {
      stop(u"Image download timed out"_s);
    });
    queue_->enqueue(this);
  }
  ~NetworkLoad() override {
    if (reply_) {
      QObject::disconnect(reply_, nullptr, this, nullptr);
      reply_->abort();
      reply_->deleteLater();
    }
    if (queue_)
      queue_->release(this);
  }
  bool done() const { return done_; }
  QString error() const { return error_; }
  qreal progress() const {
    return total_ > 0
               ? qMin(qreal(1), qreal(input_.length.to_primitive()) / total_)
               : 0;
  }
  QString identity() const {
    return url_.toString(QUrl::FullyEncoded) + QLatin1Char('\n') +
           QString::fromLatin1(hash_.result().toHex());
  }
  auto take_source() -> Result<wi::Source, wi::Error> {
    if (!done_ || !error_.isEmpty() || input_.length == usize())
      return Err(wi::Error::InvalidInput);
    const auto length = u64(input_.length.to_primitive());
    auto owner = rstd::io::SharedReadAt::try_make(rstd::move(input_));
    if (owner.is_err())
      return Err(wi::Error::ResourceLimit);
    auto range =
        rstd::io::ReadRange::make(owner.unwrap_unchecked(), u64(), length);
    if (range.is_err())
      return Err(wi::Error::InvalidInput);
    return Ok(wi::Source::reader(range.unwrap_unchecked()));
  }
};

NetworkQueue::NetworkQueue(QQmlEngine *engine)
    : QObject(engine), engine_(engine) {
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

void NetworkQueue::enqueue(NetworkLoad *load) {
  if (closing_) {
    load->stop(u"Image network engine was destroyed"_s);
    return;
  }
  waiting_.emplace_back(load);
  schedule();
}

void NetworkQueue::release(NetworkLoad *load) {
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
          auto *load = waiting_.pop().unwrap();
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
