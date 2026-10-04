#include "QExtra/image.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QPluginLoader>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkDiskCache>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlNetworkAccessManagerFactory>
#include <QtQuick/QQuickWindow>
#include <rstd/macro.hpp>
import rstd;
import qextra.image.service;
import qextra.image.network;
import qextra.image.playback;

Q_IMPORT_PLUGIN(QExtraPlugin)
using namespace rstd::prelude;

template <typename F> bool wait_for(F predicate, int timeout = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QThread::msleep(5);
  }
  return predicate();
}

class Manager : public QNetworkAccessManager {
public:
  int requests{};
  using QNetworkAccessManager::QNetworkAccessManager;
  QNetworkReply *createRequest(Operation op, const QNetworkRequest &original,
                               QIODevice *data) override {
    ++requests;
    auto request = original;
    if (request.url().path() == QLatin1String("/cached"))
      request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                           QNetworkRequest::PreferCache);
    return QNetworkAccessManager::createRequest(op, request, data);
  }
};
class Factory : public QQmlNetworkAccessManagerFactory {
public:
  QTemporaryDir cache;
  QPointer<Manager> manager;
  QNetworkAccessManager *create(QObject *parent) override {
    manager = new Manager(parent);
    auto *disk = new QNetworkDiskCache(manager);
    disk->setCacheDirectory(cache.path());
    manager->setCache(disk);
    return manager;
  }
};

class ControlledReply : public QNetworkReply {
public:
  ControlledReply(const QNetworkRequest &request, QObject *parent)
      : QNetworkReply(parent) {
    setRequest(request);
    setUrl(request.url());
    setOperation(QNetworkAccessManager::GetOperation);
    open(QIODevice::ReadOnly);
  }
  void complete(NetworkError error = NoError) {
    if (isFinished())
      return;
    setFinished(true);
    if (error != NoError) {
      setError(error, QStringLiteral("Controlled network error"));
      emit errorOccurred(error);
    }
    emit finished();
  }
  void abort() override { complete(OperationCanceledError); }
  qint64 readData(char *, qint64) override { return -1; }
};

class ControlledManager : public QNetworkAccessManager {
public:
  QList<QPointer<ControlledReply>> replies;
  QList<QUrl> started;
  int active{};
  int peak{};
  using QNetworkAccessManager::QNetworkAccessManager;
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request,
                               QIODevice *) override {
    auto *reply = new ControlledReply(request, this);
    replies.append(reply);
    started.append(request.url());
    peak = qMax(peak, ++active);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this] { --active; });
    return reply;
  }
};

class ControlledFactory : public QQmlNetworkAccessManagerFactory {
public:
  QPointer<ControlledManager> manager;
  QNetworkAccessManager *create(QObject *parent) override {
    manager = new ControlledManager(parent);
    return manager;
  }
};

void test_queue() {
  ControlledFactory first, second;
  auto *engine = new QQmlEngine;
  engine->setNetworkAccessManagerFactory(&first);
  QQmlEngine other;
  other.setNetworkAccessManagerFactory(&second);
  QList<qextra::image::NetworkLoad *> loads, otherLoads;
  auto url = [](int index) {
    return QUrl(QStringLiteral("https://host%1.invalid/image%2")
                    .arg(index % 3)
                    .arg(index));
  };
  for (int i = 0; i < 12; ++i)
    loads.append(new qextra::image::NetworkLoad(engine, url(i), nullptr));
  rstd_assert(
      wait_for([&] { return first.manager && first.manager->active == 8; }));
  rstd_assert(first.manager->started.size() == 8 && first.manager->peak == 8);
  for (int i = 0; i < 8; ++i)
    rstd_assert(first.manager->started[i] == url(11 - i));
  for (int i = 0; i < 9; ++i)
    otherLoads.append(new qextra::image::NetworkLoad(&other, url(i), nullptr));
  rstd_assert(
      wait_for([&] { return second.manager && second.manager->active == 8; }));
  rstd_assert(first.manager->active == 8);

  delete loads[1];
  loads[1] = nullptr;
  delete loads[10];
  loads[10] = nullptr;
  rstd_assert(wait_for([&] { return first.manager->started.size() == 9; }));
  rstd_assert(first.manager->started.last() == url(3));
  first.manager->replies[0]->complete();
  rstd_assert(wait_for([&] { return first.manager->started.size() == 10; }));
  rstd_assert(first.manager->started.last() == url(2));
  first.manager->replies[2]->complete(QNetworkReply::ContentNotFoundError);
  rstd_assert(wait_for([&] { return first.manager->started.size() == 11; }));
  rstd_assert(first.manager->started.last() == url(0));
  rstd_assert(!first.manager->started.contains(url(1)));
  rstd_assert(first.manager->peak == 8 && first.manager->active == 8);

  for (int i = 12; i < 15; ++i)
    loads.append(new qextra::image::NetworkLoad(engine, url(i), nullptr));
  delete engine;
  for (auto *load : loads) {
    if (load)
      rstd_assert(load->done());
    delete load;
  }
  for (auto *load : otherLoads)
    delete load;
  rstd_assert(second.manager->active == 0);
  const auto started = second.manager->started.size();
  QCoreApplication::processEvents();
  rstd_assert(second.manager->started.size() == started);
  qInfo()
      << "network queue engine isolation, limit, order and cancellation passed";
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  test_queue();
  QByteArray gif;
  QList<QByteArray> animations;
  for (int i = 1; i < argc; ++i) {
    QFile file(QString::fromLocal8Bit(argv[i]));
    rstd_assert(file.open(QIODevice::ReadOnly));
    animations.push_back(file.readAll());
  }
  QTcpServer server;
  rstd_assert(server.listen(QHostAddress::LocalHost));
  QHash<QByteArray, int> hits;
  bool blue{};
  auto url = [&](const char *path) {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                    .arg(server.serverPort())
                    .arg(QLatin1String(path)));
  };
  QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
    while (auto *socket = server.nextPendingConnection()) {
      QObject::connect(socket, &QTcpSocket::disconnected, socket,
                       &QObject::deleteLater);
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
        auto request =
            socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n") ||
            socket->property("handled").toBool())
          return;
        socket->setProperty("handled", true);
        const auto path = request.split(' ').value(1);
        ++hits[path];
        if (path == "/slow")
          return;
        QByteArray body("P6\n1 1\n255\n");
        body += path == "/changing" && blue ? QByteArray::fromHex("0000ff")
                                            : QByteArray::fromHex("ff0000");
        QByteArray status("200 OK"), extra;
        if (path == "/gif")
          body = gif;
        if (path == "/cached")
          body = QByteArray("P6\n350 350\n255\n") +
                 QByteArray(350 * 350 * 3, char(255));
        if (path == "/large" || path == "/progress")
          body = QByteArray("P6\n300 100\n255\n") +
                 QByteArray(300 * 100 * 3, char(255));
        if (path == "/bad")
          body = "not an image";
        if (path == "/missing")
          status = "404 Not Found";
        if (path == "/redirect") {
          status = "302 Found";
          extra = "Location: /still\r\n";
          body = "redirect body";
        }
        if (path == "/unsafe") {
          status = "302 Found";
          extra = "Location: file:///nonexistent-network-image\r\n";
        }
        if (path == "/huge") {
          socket->write("HTTP/1.1 200 OK\r\nContent-Length: 67108865\r\n\r\n");
          return;
        }
        if (path == "/progress") {
          socket->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                        QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body.left(32000));
          QTimer::singleShot(100, socket, [socket, tail = body.mid(32000)] {
            socket->write(tail);
            socket->disconnectFromHost();
          });
          return;
        }
        if (path == "/chunked") {
          socket->write("HTTP/1.1 200 OK\r\nTransfer-Encoding: "
                        "chunked\r\nConnection: close\r\n\r\n");
          for (const auto &chunk : {body.left(5), body.mid(5)})
            socket->write(QByteArray::number(chunk.size(), 16) + "\r\n" +
                          chunk + "\r\n");
          socket->write("0\r\n\r\n");
        } else {
          extra += path == "/cached" ? "Cache-Control: public, max-age=3600\r\n"
                                     : "Cache-Control: no-store\r\n";
          socket->write("HTTP/1.1 " + status + "\r\n" + extra +
                        "Content-Length: " + QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body);
        }
        socket->disconnectFromHost();
      });
    }
  });
  Factory factory;
  auto *engine = new QQmlEngine;
  engine->setNetworkAccessManagerFactory(&factory);
  QQmlComponent component(engine);
  component.setData(R"(
import QtQuick
import QtQuick.Window
import QExtra as QE
Window {
    visible: true; width: 64; height: 64
    QE.AnimatedImage { objectName: "image"; anchors.fill: parent; maxSize: Qt.size(512, 512) }
    QE.AnimatedImage { objectName: "peer"; anchors.fill: parent; maxSize: Qt.size(512, 512); sharedPlayback: true; visible: false }
})",
                    QUrl("qrc:/network.qml"));
  auto *root = component.create();
  if (!root) {
    qCritical() << component.errors();
    return 1;
  }
  auto *window = qobject_cast<QQuickWindow *>(root);
  auto *image = root->findChild<qextra::AnimatedImage *>("image");
  auto *peer = root->findChild<qextra::AnimatedImage *>("peer");
  rstd_assert(wait_for([&] { return window->isExposed(); }));
  auto load = [&](const char *path,
                  qextra::Image::Status expected = qextra::Image::Ready) {
    image->setSource(QUrl());
    image->setSource(url(path));
    const bool ready = wait_for([&] { return image->status() == expected; });
    if (!ready) {
      qCritical() << path << image->status() << image->errorString();
      qCritical() << "server requests" << hits << "manager requests"
                  << factory.manager->requests;
      for (auto *reply : factory.manager->findChildren<QNetworkReply *>())
        qCritical() << reply->url() << reply->isFinished()
                    << reply->bytesAvailable() << reply->errorString();
    }
    rstd_assert(ready);
  };
  auto color = [&](bool expectBlue) {
    return wait_for([&] {
      auto pixels = window->grabWindow();
      if (pixels.isNull())
        return false;
      auto c = pixels.pixelColor(pixels.width() / 2, pixels.height() / 2);
      return expectBlue ? c.blue() > 240 && c.red() < 10
                        : c.red() > 240 && c.blue() < 10;
    });
  };
  load("/still");
  rstd_assert(factory.manager && factory.manager->requests > 0);
  rstd_assert(image->sourceSize() == QSize(1, 1) && image->progress() == 1);
  rstd_assert(color(false));
  load("/redirect");
  rstd_assert(color(false));
  load("/chunked");
  rstd_assert(color(false));
  load("/large");
  rstd_assert(image->sourceSize() == QSize(300, 100));
  bool partialProgress = false;
  QObject::connect(image, &qextra::Image::progressChanged, image, [&] {
    partialProgress |= image->progress() > 0 && image->progress() < 1;
  });
  load("/progress");
  rstd_assert(partialProgress);
  load("/cached");
  const auto cached = hits["/cached"];
  load("/cached");
  rstd_assert(hits["/cached"] == cached);
  load("/changing");
  rstd_assert(color(false));
  blue = true;
  load("/changing");
  rstd_assert(color(true));
  load("/missing", qextra::Image::Error);
  load("/bad", qextra::Image::Error);
  load("/huge", qextra::Image::Error);
  load("/unsafe", qextra::Image::Error);
  image->setSource(url("/slow"));
  rstd_assert(wait_for([&] { return hits["/slow"] == 1; }));
  load("/still");
  rstd_assert(color(false));
  image->setVisible(false);
  const auto before = hits["/still"];
  image->setSource(QUrl());
  image->setSource(url("/still"));
  wait_for([] { return false; }, 100);
  rstd_assert(hits["/still"] == before);
  image->setVisible(true);
  rstd_assert(
      wait_for([&] { return image->status() == qextra::Image::Ready; }));
  for (const auto &animation : animations) {
    gif = animation;
    load("/gif");
    rstd_assert(wait_for([&] { return image->currentFrame() > 0; }));
    const auto downloads = hits["/gif"];
    image->restart();
    rstd_assert(wait_for([&] { return image->currentFrame() == 0; }));
    image->setCurrentFrame(1);
    rstd_assert(wait_for([&] { return image->currentFrame() == 1; }));
    image->setCache(false);
    qextra::image::clear_cache();
    image->restart();
    rstd_assert(wait_for([&] { return image->currentFrame() == 0; }));
    rstd_assert(wait_for([&] { return image->currentFrame() > 0; }));
    rstd_assert(hits["/gif"] == downloads);
  }
  image->setSource(url("/slow"));
  rstd_assert(wait_for([&] { return hits["/slow"] == 2; }));
  image->setSource(QUrl());
  image->setSharedPlayback(true);
  image->setCache(false);
  peer->setVisible(true);
  peer->setCache(false);
  for (const auto &animation : animations) {
    gif = animation;
    const auto opened = qextra::image::service_statistics().opened;
    image->setSource(url("/gif"));
    peer->setSource(url("/gif"));
    rstd_assert(wait_for([&] {
      return image->status() == qextra::Image::Ready &&
             peer->status() == qextra::Image::Ready;
    }));
    rstd_assert(qextra::image::playback_statistics(engine).sessions ==
                usize(1));
    rstd_assert(qextra::image::service_statistics().opened ==
                opened + usize(1));
    rstd_assert(wait_for([&] {
      return image->currentFrame() > 0 &&
             image->currentFrame() == peer->currentFrame();
    }));
    image->setSource(QUrl());
    rstd_assert(qextra::image::playback_statistics(engine).active == usize(1));
    const auto frame = peer->currentFrame();
    rstd_assert(wait_for([&] { return peer->currentFrame() != frame; }));
    peer->setSource(QUrl());
  }
  blue = false;
  load("/changing");
  blue = true;
  peer->setSource(url("/changing"));
  rstd_assert(wait_for([&] { return peer->status() == qextra::Image::Ready; }));
  rstd_assert(qextra::image::playback_statistics(engine).sessions == usize(2));
  rstd_assert(color(true));
  peer->setVisible(false);
  rstd_assert(color(false));
  peer->setSource(QUrl());
  image->setSource(url("/slow"));
  rstd_assert(wait_for([&] { return hits["/slow"] == 3; }));
  delete engine;
  rstd_assert(
      wait_for([&] { return image->status() == qextra::Image::Error; }));
  delete root;
  qextra::image::clear_cache();
  rstd_assert(wait_for(
      [] { return qextra::image::statistics().live_bytes == usize(); }));
  qInfo() << "network image lifecycle, cache and animation passed";
}
