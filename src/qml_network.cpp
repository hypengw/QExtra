module;
#include "QExtra/macro_qt.hpp"

module qextra;
import :qml_network;

using namespace Qt::Literals::StringLiterals;

namespace
{

class DiskCacheNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory {
public:
    DiskCacheNetworkAccessManagerFactory(QString               cache_dir,
                                         std::optional<qint64> maximum_cache_size)
        : m_cache_dir(std::move(cache_dir)), m_maximum_cache_size(maximum_cache_size) {}

    auto create(QObject* parent) -> QNetworkAccessManager* override {
        auto* manager = new QNetworkAccessManager(parent);
        if (! m_cache_dir.isEmpty()) {
            auto* cache = new QNetworkDiskCache(manager);
            cache->setCacheDirectory(m_cache_dir);
            if (m_maximum_cache_size) {
                cache->setMaximumCacheSize(*m_maximum_cache_size);
            }
            manager->setCache(cache);
            m_cache = cache;
        }
        return manager;
    }

    auto cache() const -> QNetworkDiskCache* { return m_cache.data(); }

    void setMaximumCacheSize(qint64 size) {
        m_maximum_cache_size = size;
        if (auto* cache = m_cache.data()) {
            cache->setMaximumCacheSize(size);
        }
    }

    auto maximumCacheSize() const -> qint64 {
        if (auto* cache = m_cache.data()) {
            return cache->maximumCacheSize();
        }
        return m_maximum_cache_size.value_or(0);
    }

private:
    QString                     m_cache_dir;
    std::optional<qint64>       m_maximum_cache_size;
    QPointer<QNetworkDiskCache> m_cache;
};

auto default_qml_network_cache_dir() -> QString {
    auto base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) return {};

    auto dir  = QDir(base);
    auto path = dir.filePath(u"qml-network"_s);
    if (! dir.mkpath(u"qml-network"_s)) {
        qWarning() << "failed to create QML network cache directory:" << path;
        return {};
    }
    return path;
}

} // namespace

class QmlNetworkDiskCache::Private {
public:
    explicit Private(QString cache_dir, std::optional<qint64> maximum_cache_size)
        : cache_dir(std::move(cache_dir)) {
        factory = std::make_unique<DiskCacheNetworkAccessManagerFactory>(this->cache_dir,
                                                                         maximum_cache_size);
    }

    QString                                               cache_dir;
    std::unique_ptr<DiskCacheNetworkAccessManagerFactory> factory;
};

QmlNetworkDiskCache::QmlNetworkDiskCache()
    : d_ptr(std::make_unique<Private>(default_qml_network_cache_dir(), std::nullopt)) {}

QmlNetworkDiskCache::QmlNetworkDiskCache(qint64 maximum_cache_size)
    : QmlNetworkDiskCache(default_qml_network_cache_dir(), maximum_cache_size) {}

QmlNetworkDiskCache::QmlNetworkDiskCache(QString cache_dir)
    : d_ptr(std::make_unique<Private>(std::move(cache_dir), std::nullopt)) {}

QmlNetworkDiskCache::QmlNetworkDiskCache(QString cache_dir, qint64 maximum_cache_size)
    : d_ptr(std::make_unique<Private>(std::move(cache_dir), maximum_cache_size)) {}

QmlNetworkDiskCache::~QmlNetworkDiskCache() = default;

void QmlNetworkDiskCache::install(QQmlEngine* engine) {
    if (! engine) return;
    engine->setNetworkAccessManagerFactory(d_ptr->factory.get());
    (void)engine->networkAccessManager();
}

void QmlNetworkDiskCache::install(QQmlEngine& engine) { install(&engine); }

auto QmlNetworkDiskCache::cacheDir() const -> QString { return d_ptr->cache_dir; }

auto QmlNetworkDiskCache::cacheSize() const -> qint64 {
    auto* cache = d_ptr->factory->cache();
    return cache ? cache->cacheSize() : 0;
}

auto QmlNetworkDiskCache::maximumCacheSize() const -> qint64 {
    return d_ptr->factory->maximumCacheSize();
}

void QmlNetworkDiskCache::setMaximumCacheSize(qint64 size) {
    d_ptr->factory->setMaximumCacheSize(size);
}

void QmlNetworkDiskCache::clear() {
    if (auto* cache = d_ptr->factory->cache()) {
        cache->clear();
    }
}
