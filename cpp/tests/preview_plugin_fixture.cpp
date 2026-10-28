#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
    const auto request = QJsonDocument::fromJson(input.readAll()).object();
    if (request.value("type").toString() == "extension") {
        if (request.value("event").toString() == "screenshot.before_export" && !qEnvironmentVariableIsEmpty("JIETUBA_EXTENSION_TEST_SLOW")) QThread::msleep(1200);
        const QString trace = qEnvironmentVariable("JIETUBA_EXTENSION_TEST_TRACE");
        if (!trace.isEmpty()) {
            QFile log(trace);
            if (log.open(QIODevice::WriteOnly | QIODevice::Append)) log.write(request.value("event").toString().toUtf8() + "\n");
        }
        auto payload = request.value("payload").toObject();
        if (request.value("event").toString() == "screenshot.before_export" && !qEnvironmentVariableIsEmpty("JIETUBA_EXTENSION_TEST_IMAGE"))
            payload.insert("data", qEnvironmentVariable("JIETUBA_EXTENSION_TEST_IMAGE"));
        if (request.value("command").toString() == "fail" || (request.value("event").toString() == "screenshot.before_export" && !qEnvironmentVariableIsEmpty("JIETUBA_EXTENSION_TEST_FAIL"))) {
            QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
            output.write(R"({"api_version":2,"ok":false})");
            return 0;
        }
        if (request.value("command").toString() == "render") {
            const QString content = payload.value("data").toObject().value("content").toString();
            if (content == "slow") QThread::msleep(3000);
            payload = {{"kind", "preview"}, {"data", QJsonObject{{"kind", "text"}, {"content", "plugin:" + content}}}};
        } else if (request.value("command").toString() == "transform") {
            payload = {{"kind", "text"}, {"data", "plugin:" + payload.value("data").toString()}};
        } else if (payload.value("kind").toString() == "text") payload.insert("data", payload.value("data").toString() + "|" + QDir::current().dirName());
        QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
        output.write(QJsonDocument(QJsonObject{{"api_version", 2}, {"ok", true}, {"payload", payload}}).toJson(QJsonDocument::Compact));
        return 0;
    }
    return 1;
}
