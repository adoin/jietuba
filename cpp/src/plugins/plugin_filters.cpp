#include "plugins/plugin_filters.h"
#include "plugins/plugin_package.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QReadWriteLock>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace jietuba::plugins {
bool allow_capture(const QByteArray &request, const QString &root, bool *failed) {
    QReadLocker guard(&plugin_lock());
    if (failed) *failed = false;
    const QDir plugins(root.isEmpty() ? plugin_directory() : root);
    struct Candidate { QFileInfo dir; QJsonObject spec; };
    QList<Candidate> candidates;
    for (const auto &dir : plugins.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
        if (dir.isJunction() || QFileInfo::exists(QDir(dir.absoluteFilePath()).filePath(".disabled"))) continue;
        const bool was_filter = QFileInfo::exists(QDir(dir.absoluteFilePath()).filePath(".capture-filter"));
        QFile file(QDir(dir.absoluteFilePath()).filePath("plugin.json"));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
            if (was_filter) { if (failed) *failed = true; return false; }
            continue;
        }
        const auto spec = QJsonDocument::fromJson(file.readAll()).object();
        QJsonObject hook;
        for (const auto &value : spec.value("contributes").toObject().value("hooks").toArray())
            if (value.toObject().value("event").toString() == "clipboard.before_capture") hook = value.toObject();
        if (was_filter && (spec.value("api_version").toInt() != 2 || spec.value("type").toString() != "extension" ||
                           spec.value("id").toString() != dir.fileName() || !hook.value("content_types").isArray())) {
            if (failed) *failed = true;
            return false;
        }
        if (spec.value("api_version").toInt() == 2 && spec.value("type").toString() == "extension" && spec.value("id").toString() == dir.fileName() && !hook.isEmpty()) {
            hook.insert("executable", spec.value("command"));
            candidates.append({dir, hook});
        }
    }
    if (candidates.isEmpty()) return true;
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return a.spec.value("priority").toInt() > b.spec.value("priority").toInt();
    });
    const auto input = QJsonDocument::fromJson(request);
    if (!input.isObject()) { if (failed) *failed = true; return false; }
    const auto content_type = input.object().value("content_type").toString();
    if (request.size() > 8 * 1024 * 1024) {
        for (const auto &candidate : candidates) if (candidate.spec.value("content_types").toArray().contains(content_type)) {
            if (failed) *failed = true;
            return false;
        }
        return true;
    }
    QElapsedTimer budget;
    budget.start();
    for (const auto &candidate : candidates) {
        if (!candidate.spec.value("content_types").toArray().contains(content_type)) continue;
        const QString folder = candidate.dir.canonicalFilePath();
        const QString command = candidate.spec.value("executable").toString();
        const QString program = QFileInfo(QDir(folder).filePath(command)).canonicalFilePath();
        auto fail = [&] { if (failed) *failed = true; return false; };
        if (command.isEmpty() || QFileInfo(command).isAbsolute() || program.isEmpty() ||
            !program.startsWith(folder + '/', Qt::CaseInsensitive) || !QFileInfo(program).isFile() || budget.elapsed() >= 2000) return fail();
        QProcess process;
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
        process.setProgram(program);
        process.setWorkingDirectory(folder);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("JIETUBA_PLUGIN_CONFIG", plugin_config_path(candidate.dir.fileName(), root));
        process.setProcessEnvironment(environment);
        process.start();
        if (!process.waitForStarted(std::max(1, 2000 - static_cast<int>(budget.elapsed())))) return fail();
        const QJsonObject envelope{{"api_version", 2}, {"type", "extension"}, {"event", "clipboard.before_capture"}, {"command", ""},
                                   {"payload", QJsonObject{{"kind", "clipboard"}, {"data", input.object()}}}, {"context", QJsonObject{}}};
        const QByteArray encoded = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
        if (encoded.size() > 8 * 1024 * 1024) { process.kill(); process.waitForFinished(1000); return fail(); }
        process.write(encoded);
        process.closeWriteChannel();
        QByteArray output;
        while (process.state() != QProcess::NotRunning && budget.elapsed() < 2000) {
            process.waitForReadyRead(50);
            output += process.readAllStandardOutput();
            process.readAllStandardError();
            if (output.size() > 65536) break;
        }
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); return fail(); }
        output += process.readAllStandardOutput();
        if (output.size() > 65536 || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return fail();
        const auto response = QJsonDocument::fromJson(output);
        const auto payload = response.object().value("payload").toObject();
        if (!response.isObject() || response.object().value("api_version").toInt() != 2 || !response.object().value("ok").toBool() ||
            payload.value("kind").toString() != "decision" || !payload.value("data").isBool()) return fail();
        if (!payload.value("data").toBool()) return false;
    }
    return true;
}
}
