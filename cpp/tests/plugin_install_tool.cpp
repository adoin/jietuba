#include "plugins/plugin_package.h"
#include <QCoreApplication>
#include <QTextStream>

// Small integration driver for exercising the same installer as the UI.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 3) return 2;
    const auto result = jietuba::plugins::install_plugin_package(app.arguments()[1], app.arguments()[2]);
    QTextStream out(stdout);
    out << (result.ok ? result.id : result.error) << Qt::endl;
    return result.ok ? 0 : 1;
}
