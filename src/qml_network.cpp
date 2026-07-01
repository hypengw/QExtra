module;
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkDiskCache>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlNetworkAccessManagerFactory>

module qextra;
import :qml_network;

using namespace Qt::Literals::StringLiterals;

namespace
{

class DiskCacheNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory {
public:
    explicit DiskCacheNetworkAccessManagerFactory(QString cache_dir)
        : m_cache_dir(std::move(cache_dir)) {}

    auto create(QObject* parent) -> QNetworkAccessManager* override {
        auto* manager = new QNetworkAccessManager(parent);
        if (! m_cache_dir.isEmpty()) {
            auto* cache = new QNetworkDiskCache(manager);
            cache->setCacheDirectory(m_cache_dir);
            manager->setCache(cache);
        }
        return manager;
    }

private:
    QString m_cache_dir;
};

auto default_qml_network_cache_dir() -> QString {
    auto base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) return {};

    auto dir  = QDir(base);
    auto path = dir.filePath(u"qml-network"_s);
    if (! dir.mkpath(u"qml-network"_s)) {
        qWarning("failed to create QML network cache directory: %s", qPrintable(path));
        return {};
    }
    return path;
}

} // namespace

class QmlNetworkDiskCache::Private {
public:
    explicit Private(QString cache_dir): cache_dir(std::move(cache_dir)) {
        factory = std::make_unique<DiskCacheNetworkAccessManagerFactory>(this->cache_dir);
    }

    QString                                          cache_dir;
    std::unique_ptr<QQmlNetworkAccessManagerFactory> factory;
};

QmlNetworkDiskCache::QmlNetworkDiskCache(): QmlNetworkDiskCache(default_qml_network_cache_dir()) {}

QmlNetworkDiskCache::QmlNetworkDiskCache(QString cache_dir)
    : d_ptr(std::make_unique<Private>(std::move(cache_dir))) {}

QmlNetworkDiskCache::~QmlNetworkDiskCache() = default;

void QmlNetworkDiskCache::install(QQmlEngine* engine) {
    if (! engine) return;
    engine->setNetworkAccessManagerFactory(d_ptr->factory.get());
}

void QmlNetworkDiskCache::install(QQmlEngine& engine) { install(&engine); }

auto QmlNetworkDiskCache::cacheDir() const -> QString { return d_ptr->cache_dir; }
