export module qextra:image.playback;
import qextra.qt;
import :image.service;
import rstd;

using namespace rstd::prelude;
using rstd::sync::Arc;
namespace wi = wavsen::image;

namespace qextra::image {
export qreal playback_time() {
  static const auto epoch = rstd::time::Instant::now();
  return qreal(epoch.elapsed().as_nanos().to_primitive()) / 1000000.0;
}

class PlaybackSession;
class PlaybackScheduler : public QObject {
public:
  QTimer wake, timer;
  Vec<QPointer<PlaybackSession>> sessions{
      Vec<QPointer<PlaybackSession>>::make()};
  bool closing{};
  quint64 pumps{};
  explicit PlaybackScheduler(QQmlEngine *engine);
  ~PlaybackScheduler() override;
  static PlaybackScheduler *get(QQmlEngine *engine);
  void pump();
  void shutdown();
};

struct Subscriber : QObject {
  QPointer<PlaybackSession> session;
  QPointer<QObject> receiver;
  bool active{}, notified{};
  quint64 version{};
  Subscriber(PlaybackSession *value, QObject *target);
  ~Subscriber() override;
  void notify() {
    if (notified || !receiver)
      return;
    notified = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
          notified = false;
          if (receiver)
            QMetaObject::invokeMethod(receiver, "imageReady",
                                      Qt::DirectConnection);
        },
        Qt::QueuedConnection);
  }
};

class PlaybackSession : public QObject {
public:
  PlaybackScheduler *owner;
  Request request;
  Vec<Subscriber *> subscribers{Vec<Subscriber *>::make()};
  Option<Arc<Frame>> current, candidate;
  Option<wi::Error> error;
  wi::ImageInfo info;
  quint64 version{};
  usize active;
  qreal deadline{}, remaining{};
  bool awaiting{}, ended{}, finished{};

  PlaybackSession(PlaybackScheduler *scheduler, Request input)
      : QObject(scheduler), owner(scheduler), request(rstd::move(input)) {
    request.suspend(true);
    request.observe(&owner->wake, "start");
  }
  ~PlaybackSession() override {
    request.cancel();
    for (auto *subscriber : subscribers) {
      subscriber->session = nullptr;
      subscriber->notify();
    }
  }
  void setActive(Subscriber *subscriber, bool value) {
    if (subscriber->active == value)
      return;
    subscriber->active = value;
    if (value) {
      if (active++ == usize()) {
        deadline = playback_time() + remaining;
        request.suspend(false);
      }
      subscriber->notify();
    } else if (--active == usize()) {
      remaining = qMax<qreal>(0, deadline - playback_time());
      request.suspend(true);
    }
    owner->wake.start();
  }
  void remove(Subscriber *subscriber) {
    setActive(subscriber, false);
    subscribers.retain(
        [subscriber](auto *entry) { return entry != subscriber; });
    if (subscribers.is_empty())
      delete this;
  }
  void notify() {
    for (auto *subscriber : subscribers)
      if (subscriber->active || error)
        subscriber->notify();
  }
  void tick(qreal time) {
    if (active == usize() || error || finished)
      return;
    if (awaiting) {
      auto result = request.poll();
      info = result.info;
      if (result.error) {
        error = result.error;
        awaiting = false;
        notify();
        return;
      }
      if (result.frame) {
        candidate = result.frame.take();
        awaiting = false;
      }
      if (result.eof) {
        awaiting = false;
        ended = true;
        if (!current) {
          error = Some(wi::Error::InvalidInput);
          notify();
          return;
        }
      }
    }
    bool rewind = false;
    if (ended && time >= deadline) {
      ended = false;
      if (!info.animated || info.frame_count <= u64(1)) {
        finished = true;
        request.release_idle_decoder();
        return;
      }
      rewind = true;
    }
    if (candidate && (!current || time >= deadline)) {
      const auto duration =
          qreal((*candidate)->image.duration_us.to_primitive()) / 1000.0;
      deadline =
          (!current || time > deadline + duration ? time : deadline) + duration;
      current = candidate.take();
      ++version;
      notify();
      if (!info.animated) {
        finished = true;
        request.release_idle_decoder();
        return;
      }
    }
    if (!awaiting && !candidate && !ended)
      awaiting = request.advance(rewind, 0, qint64(deadline));
  }
  Option<qreal> nextDeadline() const {
    if (active != usize() && !error && !finished && (candidate || ended))
      return Some(qreal(deadline));
    return None();
  }
};

Subscriber::Subscriber(PlaybackSession *value, QObject *target)
    : session(value), receiver(target) {}
Subscriber::~Subscriber() {
  if (session)
    session->remove(this);
}

PlaybackScheduler::PlaybackScheduler(QQmlEngine *engine) : QObject(engine) {
  wake.setSingleShot(true);
  timer.setSingleShot(true);
  timer.setTimerType(Qt::PreciseTimer);
  QObject::connect(&wake, &QTimer::timeout, this, [this] { pump(); });
  QObject::connect(&timer, &QTimer::timeout, this, [this] { pump(); });
  QObject::connect(engine, &QObject::destroyed, this, [this] { shutdown(); });
}
PlaybackScheduler::~PlaybackScheduler() { shutdown(); }
void PlaybackScheduler::shutdown() {
  if (closing)
    return;
  closing = true;
  for (auto &session : sessions)
    delete session.data();
  sessions.clear();
  wake.stop();
  timer.stop();
}
PlaybackScheduler *PlaybackScheduler::get(QQmlEngine *engine) {
  for (auto *child : engine->children())
    if (auto *scheduler = dynamic_cast<PlaybackScheduler *>(child))
      return scheduler;
  return new PlaybackScheduler(engine);
}
void PlaybackScheduler::pump() {
  if (closing)
    return;
  ++pumps;
  timer.stop();
  sessions.retain([](const auto &session) { return !session.isNull(); });
  const auto time = playback_time();
  Option<qreal> next;
  for (auto &session : sessions) {
    session->tick(time);
    auto deadline = session->nextDeadline();
    if (deadline && (!next || *deadline < *next))
      next = deadline;
  }
  if (next)
    timer.start(
        int(qBound<qreal>(0, qCeil(*next - playback_time()), 2147483647)));
}

export struct PlaybackFrame {
  Option<Arc<Frame>> frame;
  Option<wi::Error> error;
  wi::ImageInfo info;
  quint64 version{};
  qreal remaining{};
};

export class Playback {
  Box<Subscriber> subscriber_;
  explicit Playback(Box<Subscriber> subscriber)
      : subscriber_(rstd::move(subscriber)) {}

public:
  Playback(Playback &&) noexcept = default;
  auto operator=(Playback &&) noexcept -> Playback & = default;
  static auto open(QQmlEngine *engine, Request request, QObject *receiver,
                   bool active) -> Result<Playback, wi::Error> {
    auto *scheduler = PlaybackScheduler::get(engine);
    if (scheduler->closing)
      return Err(wi::Error::Cancelled);
    PlaybackSession *session = nullptr;
    scheduler->sessions.retain(
        [](const auto &value) { return !value.isNull(); });
    for (auto &value : scheduler->sessions) {
      if (value->request.same_resource(request)) {
        session = value;
        break;
      }
    }
    if (!session) {
      session = new PlaybackSession(scheduler, rstd::move(request));
      scheduler->sessions.emplace_back(session);
    }
    auto subscriber = Box<Subscriber>::make(session, receiver);
    session->subscribers.emplace_back(&*subscriber);
    session->setActive(&*subscriber, active);
    return Ok(Playback(rstd::move(subscriber)));
  }
  void suspend(bool value) {
    if (subscriber_->session)
      subscriber_->session->setActive(&*subscriber_, !value);
  }
  void refresh() { subscriber_->version = 0; }
  Option<wi::Source> owned_source() const {
    return subscriber_->session ? subscriber_->session->request.owned_source()
                                : Option<wi::Source>{};
  }
  PlaybackFrame snapshot(bool onlyChanged = true) {
    auto *session = subscriber_->session.data();
    if (!session)
      return {{}, Some(wi::Error::Cancelled), {}};
    PlaybackFrame result{
        {},
        session->error,
        session->info,
        session->version,
        session->active == usize()
            ? session->remaining
            : qMax<qreal>(0, session->deadline - playback_time())};
    if (session->current &&
        (!onlyChanged || subscriber_->version != session->version)) {
      result.frame = Some((*session->current).clone());
      subscriber_->version = session->version;
    }
    return result;
  }
};

export struct PlaybackStatistics {
  usize sessions, active;
  quint64 pumps{};
};
export PlaybackStatistics playback_statistics(QQmlEngine *engine) {
  PlaybackStatistics result;
  auto *scheduler = PlaybackScheduler::get(engine);
  result.pumps = scheduler->pumps;
  for (const auto &session : scheduler->sessions) {
    if (session) {
      ++result.sessions;
      result.active += session->active;
    }
  }
  return result;
}
} // namespace qextra::image
