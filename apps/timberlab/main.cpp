#include "accept.hpp"
#include "bench.hpp"
#include "self_check.hpp"

#include "DocumentController.hpp"
#include "ViewportItem.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QTranslator>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// QML loads from the binary and from local files only. A remote URL, from rich
// text or anything else, becomes an unknown-protocol error rather than a
// request that would reveal the user's address. The bunker socket does not use
// this manager.
class LocalOnlyAccessManager : public QNetworkAccessManager {
 public:
  using QNetworkAccessManager::QNetworkAccessManager;

 protected:
  QNetworkReply* createRequest(Operation op, const QNetworkRequest& request, QIODevice* outgoing) override {
    const QString scheme = request.url().scheme();
    if (scheme == QLatin1String("qrc") || scheme == QLatin1String("file") || scheme == QLatin1String("data")) {
      return QNetworkAccessManager::createRequest(op, request, outgoing);
    }
    return QNetworkAccessManager::createRequest(op, QNetworkRequest(QUrl(QStringLiteral("blocked:"))), outgoing);
  }
};

class LocalOnlyFactory : public QQmlNetworkAccessManagerFactory {
 public:
  QNetworkAccessManager* create(QObject* parent) override { return new LocalOnlyAccessManager(parent); }
};

}  // namespace

int main(int argc, char* argv[]) {
  bool accept = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--check") return timberlab::runSelfCheck();
    if (arg == "--bench") {
      QCoreApplication core(argc, argv);
      return timberlab::runBench();
    }
    if (arg == "--bench-project" && i + 2 < argc) return timberlab::writeBenchProject(argv[i + 1], std::atoi(argv[i + 2]));
    if (arg == "--accept") accept = true;
  }
  if (accept) {
    QCoreApplication core(argc, argv);
    return timberlab::runAccept();
  }

  QSurfaceFormat format;
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
#if defined(Q_OS_ANDROID)
  // GLES 3.0. A desktop core profile is not a valid Android surface.
  format.setRenderableType(QSurfaceFormat::OpenGLES);
  format.setVersion(3, 0);
  format.setProfile(QSurfaceFormat::NoProfile);
#else
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
#endif
  // The frame-rate run draws as fast as the GPU lets it, not at the refresh rate.
  const int benchSeconds = qEnvironmentVariableIntValue("TIMBERLAB_FRAME_BENCH");
  if (benchSeconds > 0) format.setSwapInterval(0);
  QSurfaceFormat::setDefaultFormat(format);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

  QGuiApplication app(argc, argv);
  QTranslator translator;
  const QString localeName = QLocale().name();
  if (translator.load(QStringLiteral(":/i18n/timberlab_") + localeName) ||
      translator.load(QStringLiteral(":/i18n/timberlab_") + localeName.section(u'_', 0, 0))) {
    app.installTranslator(&translator);
  }
  QCoreApplication::setOrganizationName("TimberLab");
  QCoreApplication::setOrganizationDomain("timberlab.local");
  QCoreApplication::setApplicationName("TimberLab");
  QCoreApplication::setApplicationVersion("0.5.0");
#if defined(Q_OS_ANDROID)
  // An Android process has no HOME. The app's private folder is the data folder.
  if (qEnvironmentVariableIsEmpty("TIMBERLAB_DATA_DIR")) {
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    qputenv("TIMBERLAB_DATA_DIR", dataDir.toUtf8());
  }
#endif
  QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/icon.png")));
  QGuiApplication::setDesktopFileName(QStringLiteral("timberlab"));
  QQuickStyle::setStyle("Material");

  bool demo = false;
  QString projectArgument;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--demo") demo = true;
    else if (!arg.empty() && arg[0] != '-') projectArgument = QString::fromStdString(arg);
  }

  qmlRegisterType<timberlab::ui::ViewportItem>("TimberLab.Render", 1, 0, "Viewport");

  timberlab::ui::DocumentController controller;
  controller.initialise();
  if (demo) controller.makeDemo();

  LocalOnlyFactory localOnly;
  QQmlApplicationEngine engine;
  engine.setNetworkAccessManagerFactory(&localOnly);
  engine.rootContext()->setContextProperty("app", &controller);
  engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine.rootObjects().isEmpty()) return 1;
  if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)))
    window->setIcon(QGuiApplication::windowIcon());
  if (!projectArgument.isEmpty()) controller.loadProject(QUrl::fromLocalFile(projectArgument));

  if (benchSeconds > 0) {
    // Orbit the camera one step per swapped frame. Each step asks for the next
    // frame, so the count over the run is the frame rate of the whole window.
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
    auto* viewport = window != nullptr ? window->findChild<timberlab::ui::ViewportItem*>() : nullptr;
    if (viewport == nullptr) return 1;
    window->setProperty("allowDirtyQuit", true);
    auto* frames = new qint64(0);
    auto* started = new QElapsedTimer();
    QObject::connect(window, &QQuickWindow::frameSwapped, viewport, [viewport, frames, started, benchSeconds, window]() {
      if (!started->isValid()) started->start();
      ++*frames;
      if (started->elapsed() >= benchSeconds * 1000) {
        const double seconds = static_cast<double>(started->elapsed()) / 1000.0;
        std::printf("frame bench: %lld frames in %.2f s = %.1f frames/s, %.2f ms per frame, window %d x %d, "
                    "%zu parts\n",
                    static_cast<long long>(*frames), seconds, static_cast<double>(*frames) / seconds,
                    1000.0 * seconds / static_cast<double>(*frames), window->width(), window->height(),
                    static_cast<std::size_t>(viewport->partCount()));
        QCoreApplication::exit(0);
        return;
      }
      viewport->orbitStep(2.0, 0.0);
    });
    QTimer::singleShot(500, viewport, [viewport]() { viewport->orbitStep(1.0, 0.0); });
  }

  const QString screenshot = qEnvironmentVariable("TIMBERLAB_SCREENSHOT");
  if (!screenshot.isEmpty()) {
    QTimer::singleShot(1500, &app, [&engine, screenshot]() {
      auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
      if (window != nullptr) {
        window->setProperty("allowDirtyQuit", true);
        window->grabWindow().save(screenshot);
      }
      QCoreApplication::quit();
    });
  }
  return app.exec();
}
