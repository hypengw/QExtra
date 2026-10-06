#include "tests.hpp"
#include <QtCore/QPluginLoader>

import qextra.qt;

Q_IMPORT_PLUGIN(QExtraPlugin)

namespace {
struct TestGroup {
  const char *name;
  int (*run)(int, char **);
};

constexpr TestGroup groups[] = {
    {"image", run_image},
    {"image-service", run_image_service},
    {"image-network", run_image_network},
    {"image-playback", run_image_playback},
    {"kstore", run_kstore},
    {"bindable", run_bindable},
};
} // namespace

int main(int argc, char **argv) {
  const auto option = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
  if (option == "--group") {
    if (argc < 3) {
      qCritical("--group requires a test group name");
      return 2;
    }
    for (const auto &group : groups)
      if (QString::fromLocal8Bit(argv[2]) == QLatin1String(group.name))
        return group.run(argc - 2, argv + 2);
    qCritical() << "Unknown test group:" << argv[2];
    return 2;
  }
  if (option == "--list") {
    for (const auto &group : groups)
      qInfo("%s", group.name);
    return 0;
  }
  if (option == "--help" || option == "-h") {
    qInfo("Usage: qextra-tests [--list | --group NAME] [animation ...]");
    return 0;
  }
  if (option.startsWith("--")) {
    qCritical() << "Unknown option:" << option;
    return 2;
  }

  QCoreApplication app(argc, argv);
  const auto inputs = app.arguments().mid(1);
  int result = 0;
  // Isolate Qt application lifetime and the image service's global state.
  for (const auto &group : groups) {
    qInfo("Running %s", group.name);
    QProcess child;
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    child.start(app.applicationFilePath(),
                QStringList{"--group", QLatin1String(group.name)} + inputs);
    if (!child.waitForStarted()) {
      qCritical() << group.name << child.errorString();
      result = 1;
      continue;
    }
    if (!child.waitForFinished(-1) ||
        child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0) {
      qCritical() << group.name << "failed:" << child.exitCode()
                  << child.errorString();
      result = 1;
    } else {
      qInfo("Passed %s", group.name);
    }
  }
  return result;
}
