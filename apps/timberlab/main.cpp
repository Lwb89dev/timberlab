#include "self_check.hpp"

#include "DocumentController.hpp"
#include "ViewportItem.hpp"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QGuiApplication>
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

#include <string>

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--check") return timberlab::runSelfCheck();
  }

  QSurfaceFormat format;
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
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
  QCoreApplication::setApplicationVersion("0.1.0");
  QQuickStyle::setStyle("Fusion");

  bool demo = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--demo") demo = true;
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

  const QString screenshot = qEnvironmentVariable("TIMBERLAB_SCREENSHOT");
  if (!screenshot.isEmpty()) {
    QTimer::singleShot(1500, &app, [&engine, screenshot]() {
      auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
      if (window != nullptr) window->grabWindow().save(screenshot);
      QCoreApplication::quit();
    });
  }
  return app.exec();
}
