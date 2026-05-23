module;
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QGlobalStatic>
#include <QtCore/QIdentityProxyModel>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QLibrary>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QObjectBindableProperty>
#include <QtCore/QPluginLoader>
#include <QtCore/QPropertyData>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRegularExpression>
#include <QtCore/QRunnable>
#include <QtCore/QSettings>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringBuilder>
#include <QtCore/QThread>
#include <QtCore/QThreadPool>
#include <QtCore/QUuid>

#include <QtProtobuf/QProtobufSerializer>
#include <QtProtobuf/QtProtobuf>

#include <QtGui/QClipboard>
#include <QtGui/QColor>
#include <QtGui/QImageReader>
#include <QtGui/QImageWriter>
#include <QtGui/QSurfaceFormat>

#include <QtQml/QJSValueIterator>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlListProperty>
#include <QtQml/QQmlParserStatus>
#include <QtQml/QQmlPropertyMap>

#include <QtQuick/QQuickAsyncImageProvider>
#include <QtQuick/QQuickImageProvider>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <QtCore/QApplicationStatic>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngineExtensionPlugin>

export module qt;

export using ::qobject_cast;
export using ::QFlag;
export using ::QIncompatibleFlag;
export using ::QFlags;
export using ::QIncompatibleFlag;
export using ::QString;
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
export using ::QBindable;
export using ::QStandardPaths;
export using ::QProcess;

export using ::QGuiApplication;
export using ::QSurfaceFormat;
export using ::QWindowList;
export using ::QClipboard;
export using ::QImageReader;
export using ::QImageWriter;

export using ::QApplication;

export using ::QSettings;
export using ::QDataStream;
export using ::operator<<;
export using ::operator>>;

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
using Qt::ConnectionType;
using Qt::DirectConnection;
using Qt::makePropertyBinding;
using Qt::QueuedConnection;
using Qt::UniqueConnection;
} // namespace Qt

export namespace QtPrivate {
using QtPrivate::QPropertyBindingData;
}

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

export inline namespace ops {
Q_DECLARE_OPERATORS_FOR_FLAGS(QRegularExpression::PatternOptions)
Q_DECLARE_OPERATORS_FOR_FLAGS(QRegularExpression::MatchOptions)
} // namespace ops

} // namespace Qt

namespace QtProtobuf {
export using QtProtobuf::int64List;
export using QtProtobuf::int32;
export using QtProtobuf::int64;
} // namespace QtProtobuf
export using ::QProtobufSerializer;
