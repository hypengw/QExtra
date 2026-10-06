export module qextra:qml_network;
export import qextra.qt;
export import rstd.cppstd;

export class QmlNetworkDiskCache {
public:
    QmlNetworkDiskCache();
    explicit QmlNetworkDiskCache(qint64 maximum_cache_size);
    explicit QmlNetworkDiskCache(QString cache_dir);
    QmlNetworkDiskCache(QString cache_dir, qint64 maximum_cache_size);
    ~QmlNetworkDiskCache();

    void install(QQmlEngine* engine);
    void install(QQmlEngine& engine);

    auto cacheDir() const -> QString;
    auto cacheSize() const -> qint64;
    auto maximumCacheSize() const -> qint64;
    void setMaximumCacheSize(qint64 size);
    void clear();

private:
    class Private;
    std::unique_ptr<Private> d_ptr;
};
