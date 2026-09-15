#include "foundation.h"
#include "schema.h"
#include "sync_process.h"
#include "data.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <cstdio>

class LocalRequests : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const auto url = info.requestUrl();
        info.block(url.scheme() != "qrc" && url.scheme() != "data" && url.scheme() != "blob");
    }
};
class LocalPage : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;
    bool acceptNavigationRequest(const QUrl &url, NavigationType, bool mainFrame) override {
        return mainFrame && url == QUrl("qrc:/ui/index.html");
    }
};
class Window : public QMainWindow {
public:
    Foundation *bridge;
    bool closing = false;
    explicit Window(Foundation *service) : bridge(service) {
        connect(service, &Foundation::activityChanged, this, [this]() { if (closing && !bridge->busy()) close(); });
    }
    void closeEvent(QCloseEvent *event) override {
        if (!bridge->busy()) { event->accept(); return; }
        event->ignore();
        if (closing) return;
        QMessageBox question(QMessageBox::Question, "结束后台任务", "仍有后台任务运行。请求停止并退出？结构执行会等待当前语句结束；数据执行会在批次边界停止，已提交批次保留。", QMessageBox::Yes | QMessageBox::No, this);
        question.button(QMessageBox::Yes)->setText("结束并退出");
        question.button(QMessageBox::No)->setText("继续运行");
        question.setDefaultButton(QMessageBox::No);
        if (question.exec() == QMessageBox::Yes) {
            closing = true; bridge->stopJobs();
            if (!bridge->busy()) QTimer::singleShot(0, this, &QWidget::close);
        }
    }
};
int main(int argc, char **argv) {
    bool probe = argc > 1 && QByteArray(argv[1]) == "--probe";
    bool schema = argc > 1 && QByteArray(argv[1]) == "--schema";
    bool data = argc > 1 && QByteArray(argv[1]) == "--data";
    if (probe || schema || data) {
        QCoreApplication app(argc, argv);
        QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/plugins");
        QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
        const auto bytes = input.read(schema ? 16 * 1024 * 1024 + 1 : 32769);
        QJsonObject result;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (bytes.size() > (schema ? 16 * 1024 * 1024 : 32768) || error.error != QJsonParseError::NoError || !document.isObject()) result = {{"ok", false}, {"code", "validation"}, {"error", "请求格式无效"}};
        else {
            auto worker = QThread::create([&]() {
                if (data) { result = runDataCommand(document.object()); return; }
                if (probe) { result = probeDatabase(document.object()); return; }
                auto input = document.object(); auto args = input["args"].toObject();
                if (input["operation"].toString().contains("sync")) { result = runSyncCommand(input); return; }
                if (input["operation"] == "compare") {
                    auto l = args["left"].toObject(); l["action"] = "snapshot";
                    auto r = args["right"].toObject(); r["action"] = "snapshot";
                    const auto left = readSchema(input["left"].toObject(), l);
                    const auto right = readSchema(input["right"].toObject(), r);
                    result = {{"ok", true}, {"comparison", compareSchemas(left, right)}};
                } else if (input["operation"] == "schema") result = readSchema(input[args["side"].toString()].toObject(), args);
                else result = {{"ok", false}, {"code", "validation"}, {"error", "读取操作无效"}};
            });
            worker->start(); worker->wait(); delete worker;
        }
        QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
        output.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
        return 0;
    }
    QApplication app(argc, argv);
    app.setApplicationName("SpeedSyncSQL"); app.setOrganizationName("SpeedSyncSQL");
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/plugins");
    auto directory = qEnvironmentVariable("SPEED_SYNC_DATA_DIR");
    if (directory.isEmpty()) directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    Foundation service(directory);
    Window window(&service); window.resize(1280, 800); window.setMinimumSize(800, 600);
    window.setWindowTitle("Speed Sync SQL");
    auto view = new QWebEngineView(&window);
    // The profile must outlive every page; the window is destroyed before QApplication.
    auto profile = new QWebEngineProfile(&app);
    profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    auto interceptor = new LocalRequests(profile); profile->setUrlRequestInterceptor(interceptor);
    auto page = new LocalPage(profile, view); view->setPage(page);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    page->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    auto channel = new QWebChannel(page); channel->registerObject("foundation", &service); page->setWebChannel(channel);
    window.setCentralWidget(view); window.show();
    const auto smokePath = qEnvironmentVariable("SPEED_SYNC_SMOKE_REPORT");
    if (!smokePath.isEmpty()) {
        QObject::connect(page, &QWebEnginePage::loadFinished, &app, [&, smokePath](bool loaded) {
            QTimer::singleShot(2500, &app, [&, smokePath, loaded]() {
                page->runJavaScript("JSON.stringify({title:document.title,text:document.body.innerText,bridge:!!window.qt})", [&, smokePath, loaded](const QVariant &value) {
                    auto report = QJsonDocument::fromJson(value.toString().toUtf8()).object();
                    report["loaded"] = loaded; report["native"] = service.snapshot();
                    QFile file(smokePath); if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(report).toJson());
                    view->grab().save(smokePath + ".png"); app.quit();
                });
            });
        });
        QTimer::singleShot(20000, &app, &QCoreApplication::quit);
    }
    view->setUrl(QUrl("qrc:/ui/index.html"));
    return app.exec();
}
