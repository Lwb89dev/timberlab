#include "accept.hpp"
#include "self_check.hpp"

#include "DocumentController.hpp"
#include "ViewportItem.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QLocale>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QTranslator>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>

#include <string>

int main(int argc, char* argv[]) {
  bool accept = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--check") return timberlab::runSelfCheck();
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
  QCoreApplication::setApplicationVersion("0.3.0");
  QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/icon.png")));
  QGuiApplication::setDesktopFileName(QStringLiteral("timberlab"));
  QQuickStyle::setStyle("Fusion");

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

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("app", &controller);
  engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine.rootObjects().isEmpty()) return 1;
  if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)))
    window->setIcon(QGuiApplication::windowIcon());
  if (!projectArgument.isEmpty()) controller.loadProject(QUrl::fromLocalFile(projectArgument));

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
