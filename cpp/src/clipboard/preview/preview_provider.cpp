#include "clipboard/preview/preview_provider.h"
#include "clipboard/core/manager.h"
#include "core/i18n.h"
#include "plugins/plugin_package.h"
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringDecoder>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace jietuba::clipboard {
namespace {
constexpr qsizetype MAX_MESSAGE = 8 * 1024 * 1024;
constexpr int PLUGIN_TIMEOUT_MS = 1500;

std::optional<PreviewContent> external_preview(const ClipboardItem &item, ClipboardManager &manager, const QString &root, const QString &mode,
                                               const QString &selected_format) {
    const QDir plugins(root);
    const auto dirs = plugins.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    struct Candidate { QString id, dir, program; QJsonObject spec; int priority; };
    QList<Candidate> candidates;
    for (const auto &dir : dirs) {
        if (dir.isJunction()) continue;
        if (QFileInfo::exists(QDir(dir.absoluteFilePath()).filePath(".disabled"))) continue;
        QFile manifest(QDir(dir.absoluteFilePath()).filePath("plugin.json"));
        if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 65536) continue;
        const auto doc = QJsonDocument::fromJson(manifest.readAll());
        if (!doc.isObject()) continue;
        const auto manifest_data = doc.object();
        const auto spec = manifest_data.value("contributes").toObject().value("clipboard").toObject().value("preview").toObject();
        const QString id = manifest_data.value("id").toString();
        if (manifest_data.value("api_version").toInt() != 2 || manifest_data.value("type").toString() != "extension" ||
            id != dir.fileName() || !spec.value("content_types").toArray().contains(item.content_type)) continue;
        if (!manifest_data.value("contributes").toObject().value("commands").toArray().contains(spec.value("command"))) continue;
        const auto modes = spec.value("modes").toArray();
        if (modes.isEmpty() ? mode != "preview" : !modes.contains(mode)) continue;
        const QString command = manifest_data.value("command").toString();
        if (command.isEmpty() || QFileInfo(command).isAbsolute()) continue;
        const QString plugin_dir = dir.canonicalFilePath();
        const QString program = QFileInfo(QDir(plugin_dir).filePath(command)).canonicalFilePath();
        if (program.isEmpty() || !program.startsWith(plugin_dir + '/', Qt::CaseInsensitive) || !QFileInfo(program).isFile()) continue;
        candidates.append({id, plugin_dir, program, spec, std::clamp(spec.value("priority").toInt(), -1000, 1000)});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.priority > b.priority; });
    QElapsedTimer budget;
    budget.start();
    int attempted = 0;
    for (const auto &candidate : candidates) {
        if (attempted >= 8 || budget.elapsed() >= PLUGIN_TIMEOUT_MS) break;
        const auto &spec = candidate.spec;
        ++attempted;
        QJsonObject request{{"mode", mode}, {"language", I18nManager::get_current_language()}, {"content_type", item.content_type}, {"sub_kind", item.sub_kind}, {"content", item.content}};
        if (!selected_format.isEmpty()) request.insert("selected_format", selected_format);
        if (item.html_content) request.insert("html", *item.html_content);
        // Only formats explicitly requested in the installed manifest are supplied.
        QJsonObject formats;
        const auto requested = spec.value("formats").toArray();
        const QStringList available = manager.format_names(item.id);
        QJsonArray format_names;
        for (const auto &name : available) format_names.append(name);
        request.insert("format_names", format_names);
        if (requested.size() <= 8) {
            QStringList names;
            for (const auto &value : requested) {
                if (!value.isString()) continue;
                const QString name = value.toString();
                if (name.isEmpty() || name.size() > 128) continue;
                if (name == "*") {
                    if (selected_format.isEmpty()) names.append(available);
                    else if (available.contains(selected_format)) names.append(selected_format);
                }
                else names.append(name);
            }
            names.removeDuplicates();
            qsizetype total = 0;
            int examined = 0;
            for (const auto &name : names) {
                if (examined++ >= 8 || total >= 4 * 1024 * 1024) break;
                if (const auto bytes = manager.get_format(item.id, name); bytes && total + bytes->size() <= 4 * 1024 * 1024) {
                    total += bytes->size();
                    formats.insert(name, QString::fromLatin1(bytes->toBase64()));
                }
            }
        }
        if (!formats.isEmpty()) request.insert("formats_base64", formats);
        if (item.content_type == "image") {
            QByteArray image;
            if (item.image_id) image = manager.get_image_data(*item.image_id).value_or(QByteArray());
            if (image.isEmpty() && item.thumbnail && item.thumbnail->startsWith("data:image"))
                image = QByteArray::fromBase64(item.thumbnail->mid(item.thumbnail->indexOf(u',') + 1).toLatin1());
            if (image.size() <= 4 * 1024 * 1024 && !image.isEmpty()) request.insert("image_base64", QString::fromLatin1(image.toBase64()));
        }
        QJsonArray paths;
        for (const auto &path : item_paths(item)) paths.append(path);
        request.insert("paths", paths);
        const QJsonObject envelope{{"api_version", 2}, {"type", "extension"}, {"event", "command"}, {"command", spec.value("command")},
                                   {"payload", QJsonObject{{"kind", "clipboard"}, {"data", request}}}, {"context", QJsonObject{}}};
        const QByteArray input = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
        if (input.size() > MAX_MESSAGE) continue;
        QProcess process;
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
        process.setProgram(candidate.program);
        process.setWorkingDirectory(candidate.dir);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("JIETUBA_PLUGIN_CONFIG", plugins::plugin_config_path(candidate.id, root));
        process.setProcessEnvironment(environment);
        process.start();
        if (!process.waitForStarted(std::max(1, PLUGIN_TIMEOUT_MS - static_cast<int>(budget.elapsed())))) continue;
        process.write(input);
        process.closeWriteChannel();
        QByteArray output;
        bool overflow = false;
        while (process.state() != QProcess::NotRunning && budget.elapsed() < PLUGIN_TIMEOUT_MS) {
            process.waitForReadyRead(50);
            output += process.readAllStandardOutput();
            const QByteArray errors = process.readAllStandardError();
            if (output.size() > MAX_MESSAGE || errors.size() > MAX_MESSAGE) { overflow = true; break; }
        }
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
            continue;
        }
        output += process.readAllStandardOutput();
        if (overflow || output.size() > MAX_MESSAGE || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) continue;
        const auto response = QJsonDocument::fromJson(output);
        if (!response.isObject()) continue;
        const auto envelope_result = response.object();
        const auto payload = envelope_result.value("payload").toObject();
        const auto result = payload.value("data").toObject();
        if (envelope_result.value("api_version").toInt() != 2 || !envelope_result.value("ok").toBool() ||
            payload.value("kind").toString() != "preview" || !result.value("content").isString()) continue;
        const QString kind = result.value("kind").toString();
        PreviewContent preview;
        preview.provider_id = candidate.id;
        preview.text = result.value("content").toString();
        if (kind == "text") preview.kind = PreviewContent::Kind::Text;
        else if (kind == "html") preview.kind = PreviewContent::Kind::Html;
        else if (kind == "markdown") preview.kind = PreviewContent::Kind::Markdown;
        else if (kind == "image") {
            preview.kind = PreviewContent::Kind::Image;
            const auto decoded = QByteArray::fromBase64Encoding(preview.text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded || decoded.decoded.isEmpty()) continue;
            preview.image = decoded.decoded;
            QImageReader reader;
            QBuffer buffer(&preview.image);
            buffer.open(QIODevice::ReadOnly);
            reader.setDevice(&buffer);
            const QSize size = reader.size();
            if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) continue;
        } else continue;
        return preview;
    }
    return std::nullopt;
}

}

std::optional<PreviewContent> resolve_preview(const ClipboardItem &item, ClipboardManager &manager, const QString &plugin_root, const QString &mode,
                                              const QString &selected_format) {
    return external_preview(item, manager, plugin_root.isEmpty() ? plugins::plugin_directory() : plugin_root, mode, selected_format);
}
}
