#include "plugins/extension_host.h"
#include "plugins/plugin_package.h"
#include "core/i18n.h"
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QReadWriteLock>
#include <QThread>
#include <algorithm>
#include <memory>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace jietuba::plugins {
namespace {
constexpr int MAX_JSON = 8 * 1024 * 1024;
constexpr int MAX_PIXELS = 64 * 1024 * 1024;
struct Spec { QString id, folder, executable; QJsonObject contributions; };
struct Hook { QString id; int priority; };

// Caller holds plugin_lock, preventing updates while a program is being started or run.
QList<Spec> specifications(const QString &root) {
    QList<Spec> result;
    for (const auto &dir : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
        if (dir.isJunction() || QFileInfo::exists(QDir(dir.absoluteFilePath()).filePath(".disabled"))) continue;
        QFile file(QDir(dir.absoluteFilePath()).filePath("plugin.json"));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) continue;
        const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
        const QString id = manifest.value("id").toString();
        if (manifest.value("api_version").toInt() != 2 || manifest.value("type").toString() != "extension" || id != dir.fileName()) continue;
        const QString command = manifest.value("command").toString();
        const QString folder = dir.canonicalFilePath();
        if (folder.isEmpty() || (!command.isEmpty() && QFileInfo(command).isAbsolute())) continue;
        if (command.isEmpty()) { result.append({id, folder, {}, manifest.value("contributes").toObject()}); continue; }
        const QString executable = QFileInfo(QDir(folder).filePath(command)).canonicalFilePath();
        if (folder.isEmpty() || executable.isEmpty() || !executable.startsWith(folder + '/', Qt::CaseInsensitive) || !QFileInfo(executable).isFile()) continue;
        result.append({id, folder, executable, manifest.value("contributes").toObject()});
    }
    return result;
}

bool valid_payload(const QJsonObject &payload) {
    const QString kind = payload.value("kind").toString();
    if (kind == "none") return true;
    if (kind == "clipboard" || kind == "backup") return payload.value("data").isObject();
    if (kind == "decision") return payload.value("data").isBool();
    if (kind == "preview") {
        const auto preview = payload.value("data").toObject();
        return payload.value("data").isObject() && preview.value("content").isString() &&
               QStringList{"text", "html", "markdown", "image"}.contains(preview.value("kind").toString());
    }
    if (!payload.value("data").isString()) return false;
    if (kind == "text") return payload.value("data").toString().size() <= MAX_JSON && !payload.value("data").toString().contains(QChar(u'\0'));
    if (kind == "image") return !payload_image(payload).isNull();
    return false;
}

ExtensionResult invoke(const QString &id, const QString &event, const QString &command, const QJsonObject &payload,
                       const QJsonObject &details, const QString &root) {
    QReadLocker guard(&plugin_lock());
    const auto specs = specifications(root);
    const auto found = std::find_if(specs.cbegin(), specs.cend(), [&](const auto &s) { return s.id == id; });
    if (found == specs.cend() || found->executable.isEmpty()) return {false, {}, id + ": unavailable"};
    if (!command.isEmpty() && !found->contributions.value("commands").toArray().contains(command)) return {false, {}, id + ": unknown command"};
    if (command.isEmpty()) {
        bool supported = false;
        for (const auto &hook : found->contributions.value("hooks").toArray())
            if (hook.toObject().value("event").toString() == event) supported = true;
        if (!supported) return {false, {}, id + ": unknown event"};
    }
    const QJsonObject input{{"api_version", 2}, {"type", "extension"}, {"event", event}, {"command", command},
                            {"payload", payload}, {"context", details}};
    const QByteArray request = QJsonDocument(input).toJson(QJsonDocument::Compact);
    if (request.size() > MAX_JSON) return {false, {}, id + ": input too large"};
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    process.setProgram(found->executable);
    process.setWorkingDirectory(found->folder);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("JIETUBA_PLUGIN_CONFIG", plugin_config_path(id, root));
    process.setProcessEnvironment(environment);
    process.start();
    if (!process.waitForStarted(2000)) return {false, {}, id + ": cannot start"};
    if (process.write(request) != request.size()) { process.kill(); process.waitForFinished(); return {false, {}, id + ": input failed"}; }
    process.closeWriteChannel();
    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning && timer.elapsed() < 5000 && output.size() <= MAX_JSON) {
        process.waitForReadyRead(100);
        output += process.readAllStandardOutput();
        process.readAllStandardError();
    }
    output += process.readAllStandardOutput();
    if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(); return {false, {}, id + ": timed out or output too large"}; }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || output.size() > MAX_JSON) return {false, {}, id + ": execution failed"};
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(output, &parse);
    const auto response = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject() || response.value("api_version").toInt() != 2 ||
        !response.value("ok").isBool() || !response.value("ok").toBool()) return {false, {}, id + ": invalid response"};
    const QJsonObject result = response.contains("payload") ? response.value("payload").toObject() : payload;
    if ((response.contains("payload") && !response.value("payload").isObject()) || !valid_payload(result)) return {false, {}, id + ": invalid payload"};
    return {true, result, {}};
}

template <typename Function>
void run_async(QObject *context, Function work, std::function<void(ExtensionResult)> callback) {
    if (!context) return;
    auto result = std::make_shared<ExtensionResult>();
    auto *thread = QThread::create([result, work = std::move(work)] { *result = work(); });
    QObject::connect(thread, &QThread::finished, context, [result, callback = std::move(callback)] { if (callback) callback(*result); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
}

QJsonObject image_payload(const QImage &image) {
    if (image.isNull() || qint64(image.width()) * image.height() > MAX_PIXELS) return {};
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") || png.size() > MAX_JSON * 3 / 4) return {};
    return {{"kind", "image"}, {"data", QString::fromLatin1(png.toBase64())}};
}

QImage payload_image(const QJsonObject &payload) {
    if (payload.value("kind").toString() != "image" || !payload.value("data").isString()) return {};
    const auto encoded = payload.value("data").toString().toLatin1();
    if (encoded.isEmpty() || encoded.size() > MAX_JSON) return {};
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) return {};
    QByteArray bytes = decoded.decoded;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > MAX_PIXELS) return {};
    return reader.read();
}

QList<ToolbarContribution> toolbar_contributions(const QString &location, const QString &plugin_root) {
    const QString root = plugin_root.isEmpty() ? plugin_directory() : plugin_root;
    QReadLocker guard(&plugin_lock());
    QList<ToolbarContribution> result;
    for (const auto &spec : specifications(root)) {
        for (const auto &value : spec.contributions.value("toolbar").toArray()) {
            const auto item = value.toObject();
            if (item.value("location").toString() != location) continue;
            const QString id = item.value("id").toString();
            const QString name = item.value("name_i18n").toObject().value(I18nManager::get_current_language()).toString(item.value("name").toString(id));
            const QString icon_file = item.value("icon").toString();
            const QString icon = icon_file.isEmpty() ? QString() : QFileInfo(QDir(spec.folder).filePath(icon_file)).canonicalFilePath();
            QList<ExtensionStep> steps;
            for (const auto &step : item.value("pipeline").toArray()) steps.append({step.toObject().value("plugin").toString(), step.toObject().value("command").toString()});
            if (!id.isEmpty() && !steps.isEmpty() && (icon.isEmpty() || icon.startsWith(spec.folder + '/', Qt::CaseInsensitive))) result.append({spec.id, id, name, icon, steps});
        }
    }
    return result;
}

bool has_extension_hooks(const QString &event, const QString &plugin_root) {
    const QString root = plugin_root.isEmpty() ? plugin_directory() : plugin_root;
    QReadLocker guard(&plugin_lock());
    for (const auto &spec : specifications(root))
        for (const auto &hook : spec.contributions.value("hooks").toArray())
            if (hook.toObject().value("event").toString() == event) return true;
    return false;
}

ExtensionResult dispatch_hooks(const QString &event, const QJsonObject &payload, const QJsonObject &details, const QString &plugin_root) {
    const QString root = plugin_root.isEmpty() ? plugin_directory() : plugin_root;
    QList<Hook> hooks;
    {
        QReadLocker guard(&plugin_lock());
        for (const auto &spec : specifications(root)) for (const auto &value : spec.contributions.value("hooks").toArray()) {
            const auto hook = value.toObject();
            if (hook.value("event").toString() == event) hooks.append({spec.id, hook.value("priority").toInt()});
        }
    }
    std::sort(hooks.begin(), hooks.end(), [](const auto &a, const auto &b) { return a.priority != b.priority ? a.priority > b.priority : a.id < b.id; });
    QJsonObject current = payload;
    for (const auto &hook : hooks) {
        auto result = invoke(hook.id, event, {}, current, details, root);
        if (!result.ok) return result;
        current = result.payload;
    }
    return {true, current, {}};
}

ExtensionResult run_extension_pipeline(const QList<ExtensionStep> &steps, const QJsonObject &payload, const QJsonObject &details, const QString &plugin_root) {
    const QString root = plugin_root.isEmpty() ? plugin_directory() : plugin_root;
    if (steps.isEmpty() || steps.size() > 8 || !valid_payload(payload)) return {false, {}, "Invalid pipeline"};
    QJsonObject current = payload;
    for (const auto &step : steps) {
        auto result = invoke(step.plugin, "command", step.command, current, details, root);
        if (!result.ok) return result;
        current = result.payload;
    }
    return {true, current, {}};
}

void dispatch_hooks_async(const QString &event, const QJsonObject &payload, const QJsonObject &details, QObject *context,
                          std::function<void(ExtensionResult)> callback, const QString &root) {
    run_async(context, [=] { return dispatch_hooks(event, payload, details, root); }, std::move(callback));
}
void run_extension_pipeline_async(const QList<ExtensionStep> &steps, const QJsonObject &payload, const QJsonObject &details, QObject *context,
                                  std::function<void(ExtensionResult)> callback, const QString &root) {
    run_async(context, [=] { return run_extension_pipeline(steps, payload, details, root); }, std::move(callback));
}

ExtensionSession::ExtensionSession(QObject *parent, const QString &plugin_root) : QObject(parent), root_(plugin_root) {}
void ExtensionSession::hook(const QString &event, const QJsonObject &payload, const QJsonObject &details,
                            QObject *context, std::function<void(ExtensionResult)> callback) {
    if (!has_extension_hooks(event, root_)) {
        if (callback && context) enqueue({[payload] { return ExtensionResult{true, payload, {}}; }, context, std::move(callback)});
        return;
    }
    const QString root = root_;
    enqueue({[=] { return dispatch_hooks(event, payload, details, root); }, context, std::move(callback)});
}
void ExtensionSession::pipeline(const QList<ExtensionStep> &steps, const QJsonObject &payload, const QJsonObject &details,
                                QObject *context, std::function<void(ExtensionResult)> callback) {
    const QString root = root_;
    enqueue({[=] { return run_extension_pipeline(steps, payload, details, root); }, context, std::move(callback)});
}
void ExtensionSession::finish() { finished_ = true; if (!running_ && jobs_.isEmpty()) deleteLater(); }
void ExtensionSession::enqueue(Job job) { if (!finished_) { jobs_.enqueue(std::move(job)); next(); } }
void ExtensionSession::next() {
    if (running_) return;
    if (jobs_.isEmpty()) { if (finished_) deleteLater(); return; }
    running_ = true;
    auto job = jobs_.dequeue();
    auto result = std::make_shared<ExtensionResult>();
    auto *thread = QThread::create([work = std::move(job.work), result] { *result = work(); });
    QObject::connect(thread, &QThread::finished, this, [this, job, result] {
        running_ = false;
        if (job.context && job.callback) job.callback(*result);
        next();
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
}
