export module qextra:qml_network;
export import qt;
export import rstd.cppstd;

export class QmlNetworkDiskCache {
public:
    QmlNetworkDiskCache();
    explicit QmlNetworkDiskCache(QString cache_dir);
    ~QmlNetworkDiskCache();

    void install(QQmlEngine* engine);
    void install(QQmlEngine& engine);

    auto cacheDir() const -> QString;

private:
    class Private;
    std::unique_ptr<Private> d_ptr;
};
