#pragma once
#include <QByteArray>
#include <QString>
namespace jietuba::plugins {
// Fail closed if an enabled matching filter cannot be run or returns invalid data.
bool allow_capture(const QByteArray &request, const QString &root = {}, bool *failed = nullptr);
}
