module;
#include <QtCore/QAbstractListModel>
#include <QtCore/QAssociativeIterable>
#include <QtCore/QChar>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QGlobalStatic>
#include <QtCore/QHash>
#include <QtCore/QIdentityProxyModel>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QLibrary>
#include <QtCore/QLibraryInfo>
#include <QtCore/QList>
#include <QtCore/QLocale>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMetaProperty>
#include <QtCore/QObject>
#include <QtCore/QObjectBindableProperty>
#include <QtCore/QPluginLoader>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QPropertyData>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRegularExpression>
#include <QtCore/QRunnable>
#include <QtCore/QSettings>
#include <QtCore/QSequentialIterable>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/QStringBuilder>
#include <QtCore/QStringList>
#include <QtCore/QStringListModel>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QThreadPool>
#include <QtCore/QTimer>
#include <QtCore/QTranslator>
#include <QtCore/QUuid>
#include <QtCore/QVariant>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtCore/QVersionNumber>
#include <QtCore/qnamespace.h>
#include <QtCore/qtypes.h>
#include <QtCore/QtMath>

#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkDiskCache>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusConnectionInterface>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusServiceWatcher>
#include <QtDBus/QDBusVariant>

#include <QtProtobuf/QProtobufSerializer>
#include <QtProtobuf/QtProtobuf>
#include <QtProtobuf/qtprotobuftypes.h>

#include <QtGui/QClipboard>
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImageReader>
#include <QtGui/QImageWriter>
#include <QtGui/QSurfaceFormat>

#include <QtQml/QJSValueIterator>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlInfo>
#include <QtQml/QQmlListProperty>
#include <QtQml/QQmlNetworkAccessManagerFactory>
#include <QtQml/QQmlParserStatus>
#include <QtQml/QQmlPropertyMap>

#include <QtQuick/QQuickAsyncImageProvider>
#include <QtQuick/QQuickImageProvider>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGImageNode>
#include <QtQuick/QSGRendererInterface>
#include <QtQuick/QSGTextureProvider>
#include <rhi/qrhi.h>

#include <QtCore/QApplicationStatic>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngineExtensionPlugin>

export module qextra.qt;

export using ::qMin;
export using ::qMax;
export using ::qBound;
export using ::qCeil;
export using ::qIsFinite;
export using ::QElapsedTimer;
export using ::QTemporaryDir;
export using ::QStringListModel;
export using ::QMetaProperty;
export using ::QSequentialIterable;
export using ::QAssociativeIterable;
export using ::QNetworkAccessManager;
export using ::QNetworkDiskCache;
export using ::QNetworkReply;
export using ::QNetworkRequest;
export using ::QTcpServer;
export using ::QTcpSocket;
export using ::QHostAddress;
export using ::QQmlInfo;
export using ::qmlWarning;
export using ::QQmlNetworkAccessManagerFactory;
export using ::QSGNode;
export using ::QSGImageNode;
export using ::QSGRendererInterface;
export using ::QSGTexture;
export using ::QSGTextureProvider;
export using ::QRhi;
export using ::QRhiTexture;
export using ::QRhiResourceUpdateBatch;

export using ::qobject_cast;
export using ::QFlag;
export using ::QIncompatibleFlag;
export using ::QFlags;
export using ::QString;
export using ::QChar;
export using ::QAnyStringView;
export using ::QStringView;
export using ::QUtf8StringView;
export using ::QLatin1String;
export using ::QSize;
export using ::qint16;
export using ::quint16;
export using ::qint32;
export using ::quint32;
export using ::qint64;
export using ::quint64;
export using ::qlonglong;
export using ::qulonglong;
export using ::qreal;
export using ::qsizetype;
export using ::QDateTime;
export using ::qRgb;
export using ::QColor;
export using ::QImage;
export using ::QUrl;
export using ::QUuid;
export using ::QUrlTwoFlags;
export using ::QDir;
export using ::QLibrary;
export using ::QVector2D;
export using ::QVariant;
export using ::QPointer;
export using ::QVariantList;
export using ::QStringList;
export using ::QByteArray;
export using ::QByteArrayView;
export using ::QMessageLogger;
export using ::QDebug;
export using ::QScopedPointer;
export using ::qGetPtrHelper;

export using ::QRegularExpression;
export using ::QRegularExpressionMatch;
export using ::QRegularExpressionMatchIterator;

export using ::QList;
export using ::QHash;
export using ::QMap;
export using ::QHashIterator;
export using ::QLatin1Char;
export using ::QLatin1StringView;

export using ::QGlobalStatic;
export using ::QTimer;
export using ::QEvent;
export using ::QThread;
export using ::QThreadPool;
export using ::QFile;
export using ::QFileDevice;
export using ::QFileInfo;
export using ::QCryptographicHash;
export using ::QRandomGenerator;
export using ::qEnvironmentVariable;
export using ::QObject;
export using ::QMetaType;
export using ::QMetaObject;
export using ::QObjectBindableProperty;
export using ::QPluginLoader;
export using ::QCommandLineOption;
export using ::QCommandLineParser;
export using ::QBindable;
export using ::QStandardPaths;
export using ::QProcess;
export using ::QLibraryInfo;
export using ::QLocale;
export using ::QTranslator;
export using ::QVersionNumber;

export using ::QGuiApplication;
export using ::QSurfaceFormat;
export using ::QWindowList;
export using ::QClipboard;
export using ::QImageReader;
export using ::QImageWriter;

export using ::QApplication;

export using ::QDBusConnection;
export using ::QDBusConnectionInterface;
export using ::QDBusInterface;
export using ::QDBusMessage;
export using ::QDBusReply;
export using ::QDBusServiceWatcher;
export using ::QDBusVariant;

export using ::QSettings;
export using ::QDataStream;
export using ::operator&;
export using ::operator+;
export using ::operator<<;
export using ::operator>>;
export using ::operator^;
export using ::operator|;

export using ::QJSValue;
export using ::QJSValueIterator;
export using ::QJsonValue;
export using ::QJsonArray;
export using ::QJsonObject;
export using ::QJsonDocument;
export using ::QJsonParseError;
export using ::QQmlApplicationEngine;
export using ::QQmlPropertyMap;
export using ::QQmlEngine;
export using ::QJSEngine;
export using ::QQmlComponent;
export using ::QQmlListProperty;
export using ::QQmlEngineExtensionPlugin;
export using ::QQmlContext;
export using ::QQmlParserStatus;
export using ::qmlRegisterUncreatableType;

export using ::QQuickItem;
export using ::QQuickWindow;
#undef QT_PROPERTY_DEFAULT_BINDING_LOCATION
export constexpr auto QT_PROPERTY_DEFAULT_BINDING_LOCATION =
    QPropertyBindingSourceLocation(std::source_location::current());

export using ::QRunnable;
export using ::QPropertyData;
export using ::QPropertyBindingSourceLocation;
export using ::QUntypedPropertyData;
export using ::QPropertyNotifier;
export using ::QPropertyBinding;
export using ::QPropertyChangeHandler;
export using ::QUntypedPropertyBinding;
export using ::QBindingStorage;
export using ::qGetBindingStorage;
export using ::QProperty;

export using ::QVariantMap;
export using ::QAbstractItemModel;
export using ::QSortFilterProxyModel;
export using ::QAbstractListModel;
export using ::QIdentityProxyModel;
export using ::QModelIndex;

export namespace Qt {
using Qt::AutoConnection;
using Qt::BlockingQueuedConnection;
using Qt::CaseInsensitive;
using Qt::CaseSensitive;
using Qt::ConnectionType;
using Qt::DirectConnection;
using Qt::makePropertyBinding;
using Qt::QueuedConnection;
using Qt::PreciseTimer;
using Qt::SkipEmptyParts;
using Qt::UniqueConnection;
using Qt::UserRole;
} // namespace Qt

export namespace QtPrivate {
using QtPrivate::QPropertyBindingData;
}

export namespace QDBus {
using QDBus::Block;
using QDBus::CallMode;
} // namespace QDBus

export namespace QTypeTraits {
using QTypeTraits::is_dereferenceable;
using QTypeTraits::is_dereferenceable_v;
} // namespace QTypeTraits

export using ::QCoreApplication;

export using ::QQuickImageResponse;
export using ::QQuickTextureFactory;
export using ::QQuickImageProvider;
export using ::QQuickAsyncImageProvider;

namespace Qt {
inline namespace Literals {
inline namespace StringLiterals {
export using Qt::StringLiterals::operator""_L1;
export using Qt::StringLiterals::operator""_s;
} // namespace StringLiterals
} // namespace Literals

} // namespace Qt

namespace QtProtobuf {
export using QtProtobuf::int64List;
export using QtProtobuf::int32;
export using QtProtobuf::int64;
export using QtProtobuf::uint32;
export using QtProtobuf::uint64List;
} // namespace QtProtobuf
export using ::QProtobufSerializer;
