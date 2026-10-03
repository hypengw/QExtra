export module qextra.image.service;
import qt;
export import wavsen.image;
import rstd;

using namespace rstd::prelude;
using rstd::sync::Arc;
using rstd::sync::Mutex;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
namespace wi = wavsen::image;

namespace qextra::image {
export constexpr int maximum_cache_mib = 512;
struct Counts {
  Atomic<usize> payloads;
  Atomic<usize> workers;
  Atomic<usize> decoded;
  Atomic<usize> opened;
  Atomic<usize> hits;
};

struct Notification {
  struct Target {
    QObject *receiver{};
    const char *method{};
    bool pending{};
  };
  Mutex<Target> target;
  void disconnect() {
    auto locked = target.lock().unwrap_unchecked();
    locked->receiver = nullptr;
  }
  static void send(Arc<Notification> notice) {
    auto locked = notice->target.lock().unwrap_unchecked();
    if (!locked->receiver || locked->pending)
      return;
    locked->pending = true;
    // Cancellation clears the receiver under this same lock before destruction.
    QMetaObject::invokeMethod(
        locked->receiver,
        [notice = rstd::move(notice)] {
          QObject *receiver;
          const char *method;
          {
            auto target = notice->target.lock().unwrap_unchecked();
            target->pending = false;
            receiver = target->receiver;
            method = target->method;
          }
          if (receiver)
            QMetaObject::invokeMethod(receiver, method, Qt::DirectConnection);
        },
        Qt::QueuedConnection);
  }
};

export struct Frame {
  wi::DecodedImage image;
  Arc<Counts> counts;
  Frame(wi::DecodedImage value, Arc<Counts> owner)
      : image(rstd::move(value)), counts(rstd::move(owner)) {}
  ~Frame() { counts->payloads.fetch_sub(usize(1), Ordering::Release); }
};

struct Fields {
  Option<wi::ImageSession> session;
  Option<Arc<Frame>> ready;
  Option<wi::Error> error;
  wi::ImageInfo info;
  bool busy{};
  bool eof{};
  bool started{};
  int next{};
  int sessionNext{};
  Vec<rstd::sync::Weak<Notification>> listeners{
      Vec<rstd::sync::Weak<Notification>>::make()};
};

struct Key {
  QString path;
  qint64 size;
  qint64 modified;
  u32 width;
  u32 height;
  quint64 context;
  int frame;
  wi::ResizeMode mode;
  u32 maxWidth;
  u32 maxHeight;
  bool operator==(const Key &) const = default;
};

struct State {
  QString path;
  Key key;
  bool animation;
  bool cache;
  Atomic<usize> subscribers{usize(1)};
  Atomic<usize> activeSubscribers{usize(1)};
  wi::DecodeRequest size;
  wi::Cancellation cancellation;
  Mutex<Fields> fields;
  State(QString p, Key k, bool a, bool cached, wi::DecodeRequest s,
        wi::Cancellation c)
      : path(rstd::move(p)), key(rstd::move(k)), animation(a), cache(cached),
        size(s), cancellation(rstd::move(c)), fields(Fields{}) {}
};

export struct Poll {
  Option<Arc<Frame>> frame;
  Option<wi::Error> error;
  wi::ImageInfo info;
  bool eof{};
};

struct Service {
  struct Job {
    Arc<State> state;
    bool rewind;
    int target;
    int priority;
    qint64 deadline;
    rstd::time::Instant queued{rstd::time::Instant::now()};
  };
  struct Queue {
    Vec<Job> jobs{Vec<Job>::make()};
    usize running;
    usize limit{usize(2)};
    bool continuation{};
    bool closing{};
  };
  struct CacheEntry {
    Key key;
    Vec<wi::DecodedImage> frames{Vec<wi::DecodedImage>::make()};
    wi::ImageInfo info;
    usize stamp;
    usize bytes;
    bool complete{};
    bool blocked{};
  };
  struct Cache {
    Vec<CacheEntry> entries{Vec<CacheEntry>::make()};
    usize bytes;
    usize clock;
    usize limit{usize(16 * 1024 * 1024)};
  };
  wi::Budget budget =
      wi::Budget::try_make(usize((maximum_cache_mib + 128) * 1024 * 1024))
          .unwrap();
  Arc<Counts> counts = Arc<Counts>::make();
  rstd::thread::ThreadPool pool = rstd::thread::ThreadPoolBuilder::make()
                                      .worker_count(usize(4))
                                      .build()
                                      .unwrap();
  Mutex<Vec<rstd::sync::Weak<State>>> requests{
      Vec<rstd::sync::Weak<State>>::make()};
  Mutex<Cache> cache;
  Mutex<Queue> queue;

  ~Service() {
    {
      auto locked = queue.lock().unwrap_unchecked();
      locked->closing = true;
      locked->jobs.clear();
    }
    rstd::move(pool).join();
  }

  void dispatch(Queue &q) {
    q.jobs.retain(
        [](const Job &job) { return !job.state->cancellation.cancelled(); });
    while (!q.closing && q.running < q.limit) {
      Option<usize> selected;
      auto rank = [&](const Job &job) {
        if (job.queued.elapsed().as_millis() >= u64(250))
          return 0;
        return job.priority == (q.continuation ? 1 : 0) ? 1 : 2;
      };
      for (usize i; i < q.jobs.len(); ++i) {
        const auto &job = q.jobs[i];
        if (job.state->activeSubscribers.load(Ordering::Acquire) == usize())
          continue;
        if (!selected || rank(job) < rank(q.jobs[*selected]) ||
            (rank(job) != 0 && rank(job) == rank(q.jobs[*selected]) &&
             job.priority == 1 && q.jobs[*selected].priority == 1 &&
             job.deadline < q.jobs[*selected].deadline))
          selected = Some(i);
      }
      if (!selected)
        break;
      auto job = q.jobs.remove(*selected);
      q.continuation = job.priority == 0;
      ++q.running;
      counts->workers.fetch_add(usize(1), Ordering::Relaxed);
      counts->payloads.fetch_add(usize(1), Ordering::Relaxed);
      auto state = job.state.clone();
      auto posted = pool.handle().post([this, job = rstd::move(job)]() mutable {
        decode(rstd::move(job.state), job.rewind, job.target);
        auto locked = queue.lock().unwrap_unchecked();
        --locked->running;
        dispatch(*locked);
      });
      if (posted.is_err()) {
        --q.running;
        counts->workers.fetch_sub(usize(1), Ordering::Release);
        counts->payloads.fetch_sub(usize(1), Ordering::Release);
        auto fields = state->fields.lock().unwrap_unchecked();
        fields->busy = false;
        fields->error = Some(wi::Error::ResourceLimit);
        notify(*fields);
      }
    }
  }
  static void notify(Fields &fields) {
    fields.listeners.retain([](const auto &weak) {
      auto listener = weak.upgrade();
      if (!listener)
        return false;
      Notification::send(rstd::move(listener));
      return true;
    });
  }

  static auto copy(const wi::DecodedImage &image) -> wi::DecodedImage {
    return {image.pixels.clone(), image.width, image.height,
            image.stride,         image.index, image.duration_us};
  }
  static auto key(const QString &path, wi::DecodeRequest size, quint64 context,
                  int frame) -> Key {
    QFileInfo file(path);
    return {
        path,           file.size(), file.lastModified().toMSecsSinceEpoch(),
        size.width,     size.height, context,
        frame,          size.mode,   size.max_width,
        size.max_height};
  }
  auto cached(Key key, int index, wi::ImageInfo &info, bool &eof)
      -> Option<wi::DecodedImage> {
    key.frame = 0;
    auto locked = cache.lock().unwrap_unchecked();
    for (auto &entry : locked->entries)
      if (entry.key == key && entry.complete) {
        entry.stamp = ++locked->clock;
        info = entry.info;
        eof = usize(index) >= entry.frames.len();
        counts->hits.fetch_add(usize(1), Ordering::Relaxed);
        return eof ? Option<wi::DecodedImage>{}
                   : Some(copy(entry.frames[usize(index)]));
      }
    return None();
  }
  void remember(Key key, wi::DecodedImage *image, wi::ImageInfo info) {
    key.frame = 0;
    auto locked = cache.lock().unwrap_unchecked();
    if (locked->limit == usize())
      return;
    auto index = locked->entries.len();
    for (usize i; i < locked->entries.len(); ++i)
      if (locked->entries[i].key == key) {
        index = i;
        break;
      }
    if (index == locked->entries.len()) {
      if (!image || image->index != u64() ||
          locked->entries.try_reserve(usize(1)).is_err())
        return;
      locked->entries.push(CacheEntry{key});
    }
    auto &entry = locked->entries[index];
    entry.stamp = ++locked->clock;
    if (entry.blocked)
      return;
    if (!image) {
      if (u64(entry.frames.len().to_primitive()) == info.frame_count) {
        entry.complete = true;
        entry.info = info;
      }
      return;
    }
    auto frameIndex = usize(image->index.to_primitive());
    if (frameIndex < entry.frames.len()) {
      *image = copy(entry.frames[frameIndex]);
      return;
    }
    if (frameIndex != entry.frames.len())
      return;
    const auto bytes = image->pixels.size();
    auto block = [&](CacheEntry &victim) {
      locked->bytes -= victim.bytes;
      victim.bytes = usize();
      victim.frames.clear();
      victim.complete = false;
      victim.blocked = true;
    };
    if (bytes > locked->limit - entry.bytes ||
        entry.frames.try_reserve(usize(1)).is_err()) {
      block(entry);
      return;
    }
    while (bytes > locked->limit - locked->bytes) {
      Option<usize> oldest;
      for (usize i; i < locked->entries.len(); ++i)
        if (i != index && locked->entries[i].bytes != usize() &&
            (!oldest ||
             locked->entries[i].stamp < locked->entries[*oldest].stamp))
          oldest = Some(i);
      if (!oldest) {
        block(entry);
        return;
      }
      block(locked->entries[*oldest]);
      locked->entries[*oldest].blocked = false;
    }
    entry.frames.push(copy(*image));
    entry.bytes += bytes;
    locked->bytes += bytes;
    entry.info = info;
    entry.complete = !info.animated;
  }
  void clear_cache() {
    auto locked = cache.lock().unwrap_unchecked();
    locked->entries.clear();
    locked->bytes = usize();
  }
  void trim() {
    {
      auto locked = cache.lock().unwrap_unchecked();
      for (auto &entry : locked->entries) {
        entry.frames.clear();
        entry.bytes = usize();
        entry.complete = false;
        entry.blocked = true;
      }
      locked->bytes = usize();
    }
    auto registry = requests.lock().unwrap_unchecked();
    for (auto &weak : *registry) {
      auto state = weak.upgrade();
      if (state &&
          state->activeSubscribers.load(Ordering::Acquire) == usize()) {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->session) {
          fields->session = None();
          fields->sessionNext = 0;
        }
      }
    }
  }

  auto source(const QString &path) -> Result<wi::Source, wi::Error> {
    auto bytes = path.toUtf8();
    auto text = rstd::str_::from_utf8(slice<u8>::from_raw_parts(
        reinterpret_cast<const rstd::byte *>(bytes.constData()),
        usize(bytes.size())));
    if (text.is_err())
      return Err(wi::Error::InvalidInput);
    if (!path.startsWith(QLatin1String(":"))) {
      auto path_buf = rstd::path::PathBuf::from(text.unwrap_unchecked());
      return wi::Source::file(path_buf.as_path());
    }
    // QFile is created, read and destroyed on this worker; no QObject crosses
    // threads.
    QFile file(path);
    if (!file.open(QFile::ReadOnly))
      return Err(wi::Error::Io);
    auto size = file.size();
    if (size <= 0 || size > 64 * 1024 * 1024)
      return Err(wi::Error::ResourceLimit);
    auto memory = wi::Pixels::allocate(budget.clone(), usize(size));
    if (memory.is_err())
      return Err(memory.unwrap_err_unchecked());
    if (file.read(reinterpret_cast<char *>(memory->data_mut()), size) != size)
      return Err(wi::Error::Io);
    return wi::Source::memory(memory.unwrap_unchecked(), *text);
  }

  void decode(Arc<State> state, bool rewind, int target) {
    Option<wi::ImageSession> session;
    int index;
    int sessionNext;
    {
      auto fields = state->fields.lock().unwrap_unchecked();
      session = fields->session.take();
      index = rewind ? target : fields->next + target;
      sessionNext = fields->sessionNext;
    }
    Option<wi::Error> error;
    Option<Arc<Frame>> frame;
    wi::ImageInfo info;
    bool eof = false;
    if (state->cache && !state->cancellation.cancelled()) {
      auto hit = cached(state->key, index, info, eof);
      if (hit.is_some()) {
        auto owner =
            Arc<Frame>::try_make(hit.unwrap_unchecked(), counts.clone());
        if (owner.is_err())
          error = Some(wi::Error::ResourceLimit);
        else
          frame = Some(owner.unwrap_unchecked());
      }
    }
    if (frame || eof)
      session = None();
    for (int attempt = 0; attempt < 2 && !frame && !eof; ++attempt) {
      error = None();
      if (state->cancellation.cancelled()) {
        error = Some(wi::Error::Cancelled);
        break;
      }
      if (session && (rewind || sessionNext > index)) {
        session = None();
      }
      if (!session) {
        auto input = source(state->path);
        if (input.is_err())
          error = Some(input.unwrap_err_unchecked());
        else {
          auto opened =
              wi::ImageSession::open(input.unwrap_unchecked(), budget.clone(),
                                     state->cancellation.clone());
          if (opened.is_err())
            error = Some(opened.unwrap_err_unchecked());
          else
            session = Some(opened.unwrap_unchecked());
          counts->opened.fetch_add(usize(1), Ordering::Relaxed);
          sessionNext = 0;
        }
      }
      if (!error) {
        for (; sessionNext <= index; ++sessionNext) {
          counts->decoded.fetch_add(usize(1), Ordering::Relaxed);
          auto decoded = session->next(state->size);
          if (decoded.is_err()) {
            error = Some(decoded.unwrap_err_unchecked());
            break;
          }
          auto value = decoded.unwrap_unchecked();
          if (value.is_none()) {
            if (target > 0)
              error = Some(wi::Error::InvalidInput);
            eof = true;
            if (state->cache)
              remember(state->key, nullptr, session->info());
            break;
          }
          if (state->cache)
            remember(state->key, &*value, session->info());
          if (sessionNext == index) {
            auto owner =
                Arc<Frame>::try_make(value.unwrap_unchecked(), counts.clone());
            if (owner.is_err())
              error = Some(wi::Error::ResourceLimit);
            else
              frame = Some(owner.unwrap_unchecked());
          }
        }
      }
      if (!error || *error != wi::Error::ResourceLimit)
        break;
      session = None();
      trim();
    }
    if (frame.is_none())
      counts->payloads.fetch_sub(usize(1), Ordering::Release);
    {
      auto fields = state->fields.lock().unwrap_unchecked();
      fields->info = session.is_some() ? session->info() : info;
      fields->next = index + (frame ? 1 : 0);
      fields->sessionNext = session ? sessionNext : 0;
      if (state->animation)
        fields->session = rstd::move(session);
      fields->ready = rstd::move(frame);
      fields->error = error;
      fields->eof = eof;
      fields->busy = false;
      notify(*fields);
    }
    counts->workers.fetch_sub(usize(1), Ordering::Release);
  }
};

Service &service() {
  static Service instance;
  return instance;
}

export class Request {
  Arc<State> state_;
  Option<Arc<Notification>> notification_;
  bool suspended_{};
  explicit Request(Arc<State> state) : state_(rstd::move(state)) {}

public:
  Request(Request &&other) noexcept
      : state_(rstd::move(other.state_)),
        notification_(other.notification_.take()),
        suspended_(other.suspended_) {}
  auto operator=(Request &&other) noexcept -> Request & {
    if (this != &other) {
      cancel();
      state_ = rstd::move(other.state_);
      notification_ = other.notification_.take();
      suspended_ = other.suspended_;
    }
    return *this;
  }
  ~Request() { cancel(); }
  void cancel() {
    if (notification_) {
      (*notification_)->disconnect();
      notification_ = None();
    }
    if (state_) {
      if (!suspended_)
        state_->activeSubscribers.fetch_sub(usize(1), Ordering::AcqRel);
      if (state_->subscribers.fetch_sub(usize(1), Ordering::AcqRel) == usize(1))
        state_->cancellation.cancel();
      state_.reset();
      auto &s = service();
      auto queue = s.queue.lock().unwrap_unchecked();
      s.dispatch(*queue);
    }
  }

  static auto open(QString path, wi::DecodeRequest size, bool animation = false,
                   bool cache = true, quint64 context = 0, int initialFrame = 0)
      -> Result<Request, wi::Error> {
    auto &s = service();
    auto key = Service::key(path, size, context, initialFrame);
    auto registry = s.requests.lock().unwrap_unchecked();
    registry->retain([](const auto &weak) {
      auto state = weak.upgrade();
      return state && !state->cancellation.cancelled();
    });
    for (auto &weak : *registry) {
      auto state = weak.upgrade();
      if (state && !animation && cache && !state->animation && state->cache &&
          state->key == key && !state->cancellation.cancelled()) {
        auto count = state->subscribers.load(Ordering::Relaxed);
        while (count != usize() && count != usize::MAX) {
          if (state->subscribers.compare_exchange_weak(count, count + usize(1),
                                                       Ordering::AcqRel,
                                                       Ordering::Relaxed)) {
            state->activeSubscribers.fetch_add(usize(1), Ordering::AcqRel);
            return Ok(Request(rstd::move(state)));
          }
        }
      }
    }
    if (registry->try_reserve(usize(1)).is_err())
      return Err(wi::Error::ResourceLimit);
    auto cancellation = wi::Cancellation::make();
    if (cancellation.is_err()) {
      return Err(wi::Error::ResourceLimit);
    }
    auto state =
        Arc<State>::try_make(rstd::move(path), rstd::move(key), animation,
                             cache, size, cancellation.unwrap_unchecked());
    if (state.is_err()) {
      return Err(wi::Error::ResourceLimit);
    }
    registry->push(state->downgrade());
    return Ok(Request(state.unwrap_unchecked()));
  }

  void observe(QObject *receiver, const char *method) {
    if (notification_)
      (*notification_)->disconnect();
    auto notice = Arc<Notification>::make();
    {
      auto target = notice->target.lock().unwrap_unchecked();
      target->receiver = receiver;
      target->method = method;
    }
    auto fields = state_->fields.lock().unwrap_unchecked();
    fields->listeners.push(notice.downgrade());
    if (fields->ready || fields->error || fields->eof)
      Notification::send(notice.clone());
    notification_ = Some(rstd::move(notice));
  }

  void suspend(bool value) {
    if (suspended_ == value)
      return;
    suspended_ = value;
    if (value) {
      state_->activeSubscribers.fetch_sub(usize(1), Ordering::AcqRel);
    } else {
      state_->activeSubscribers.fetch_add(usize(1), Ordering::AcqRel);
    }
    auto &s = service();
    auto queue = s.queue.lock().unwrap_unchecked();
    s.dispatch(*queue);
  }

  bool advance(bool rewind = false, int target = 0, qint64 deadline = 0) {
    auto &s = service();
    bool first;
    {
      auto fields = state_->fields.lock().unwrap_unchecked();
      if (!state_->animation &&
          (fields->busy || fields->ready.is_some() || fields->error.is_some()))
        return true;
      if (state_->cancellation.cancelled() || fields->busy ||
          fields->ready.is_some() || fields->error.is_some() || target < 0)
        return false;
      first = !fields->started;
      fields->started = true;
      fields->busy = true;
      fields->eof = false;
    }
    auto queue = s.queue.lock().unwrap_unchecked();
    if (queue->closing || queue->jobs.try_reserve(usize(1)).is_err()) {
      auto fields = state_->fields.lock().unwrap_unchecked();
      fields->busy = false;
      fields->error = Some(wi::Error::ResourceLimit);
      Service::notify(*fields);
      return true;
    }
    queue->jobs.push(
        Service::Job{state_.clone(), rewind, target, first ? 0 : 1, deadline});
    s.dispatch(*queue);
    return true;
  }

  auto poll() -> Poll {
    auto fields = state_->fields.lock().unwrap_unchecked();
    if (!state_->animation)
      return {fields->ready.is_some() ? Some((*fields->ready).clone())
                                      : Option<Arc<Frame>>{},
              fields->error, fields->info, fields->eof};
    return {fields->ready.take(), fields->error.take(), fields->info,
            fields->eof};
  }
};

export auto statistics() -> rstd::alloc::BudgetStatistics {
  return service().budget.statistics();
}
export auto payload_count() -> usize {
  return service().counts->payloads.load(Ordering::Acquire);
}
export void clear_cache() { service().clear_cache(); }
export void set_cache_limit(usize bytes) {
  auto &s = service();
  auto locked = s.cache.lock().unwrap_unchecked();
  bytes = bytes.min(usize(maximum_cache_mib * 1024 * 1024));
  if (locked->limit == bytes)
    return;
  locked->limit = bytes;
  locked->entries.clear();
  locked->bytes = usize();
}
export struct ServiceStatistics {
  usize running, waiting, decoded, opened, cache_hits, cache_bytes, cache_limit;
};
export auto service_statistics() -> ServiceStatistics {
  auto &s = service();
  ServiceStatistics result;
  {
    auto locked = s.queue.lock().unwrap_unchecked();
    result.running = locked->running;
    result.waiting = locked->jobs.len();
  }
  {
    auto locked = s.cache.lock().unwrap_unchecked();
    result.cache_bytes = locked->bytes;
    result.cache_limit = locked->limit;
  }
  result.decoded = s.counts->decoded.load(Ordering::Acquire);
  result.opened = s.counts->opened.load(Ordering::Acquire);
  result.cache_hits = s.counts->hits.load(Ordering::Acquire);
  return result;
}
export bool set_worker_limit(usize limit) {
  if (limit < usize(1) || limit > usize(4))
    return false;
  auto &s = service();
  auto locked = s.queue.lock().unwrap_unchecked();
  locked->limit = limit;
  s.dispatch(*locked);
  return true;
}
} // namespace qextra::image
