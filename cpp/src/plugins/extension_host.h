#pragma once
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <functional>

namespace jietuba::plugins {
struct ExtensionStep { QString plugin, command; };
struct ToolbarContribution { QString plugin, id, name, icon; QList<ExtensionStep> pipeline; };
struct ExtensionResult { bool ok = false; QJsonObject payload; QString error; };

// Payloads are typed values, so the result of one command becomes the next one's input.
QJsonObject image_payload(const QImage &image);
QImage payload_image(const QJsonObject &payload);
QList<ToolbarContribution> toolbar_contributions(const QString &location, const QString &root = {});
bool has_extension_hooks(const QString &event, const QString &root = {});
ExtensionResult dispatch_hooks(const QString &event, const QJsonObject &payload, const QJsonObject &context = {}, const QString &root = {});
ExtensionResult run_extension_pipeline(const QList<ExtensionStep> &steps, const QJsonObject &payload,
                                       const QJsonObject &context = {}, const QString &root = {});
// Work is performed away from the UI thread. Destroying context suppresses its callback.
void dispatch_hooks_async(const QString &event, const QJsonObject &payload, const QJsonObject &details,
                          QObject *context, std::function<void(ExtensionResult)> callback = {}, const QString &root = {});
void run_extension_pipeline_async(const QList<ExtensionStep> &steps, const QJsonObject &payload, const QJsonObject &details,
                                  QObject *context, std::function<void(ExtensionResult)> callback, const QString &root = {});

// Serializes all events and commands within one host session. The queue outlives
// a closed capture long enough to deliver its final notification.
class ExtensionSession : public QObject {
public:
    explicit ExtensionSession(QObject *parent = nullptr, const QString &plugin_root = {});
    void hook(const QString &event, const QJsonObject &payload, const QJsonObject &details,
              QObject *context = nullptr, std::function<void(ExtensionResult)> callback = {});
    void pipeline(const QList<ExtensionStep> &steps, const QJsonObject &payload, const QJsonObject &details,
                  QObject *context, std::function<void(ExtensionResult)> callback);
    void finish();
private:
    struct Job { std::function<ExtensionResult()> work; QPointer<QObject> context; std::function<void(ExtensionResult)> callback; };
    void enqueue(Job job);
    void next();
    QQueue<Job> jobs_;
    bool running_ = false;
    bool finished_ = false;
    QString root_;
};
}
