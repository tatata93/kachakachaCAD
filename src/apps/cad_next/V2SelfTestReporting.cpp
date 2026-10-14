#include "V2SelfTestReporting.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <QStringList>

namespace kachakacha::v2::selftest {
namespace {
QString After(const QStringList& arguments, const QString& flag)
{
    const int index = arguments.indexOf(flag);
    return index >= 0 && index + 1 < arguments.size() ? arguments[index + 1] : QString();
}
}

std::vector<SelfTestCase> WithSuite(std::vector<SelfTestCase> cases, const char* suite)
{
    for (auto& item : cases) item.suite = suite;
    return cases;
}

Reporting::Reporting(const std::vector<SelfTestCase>& cases)
{
    const auto arguments = QCoreApplication::arguments();
    listOnly_ = arguments.contains(QStringLiteral("--self-test-list"));
    path_ = After(arguments, listOnly_ ? QStringLiteral("--self-test-list") : QStringLiteral("--self-test-report"));
    const QString directory = qEnvironmentVariable("KACHACAD_VERIFICATION_RUN_DIR");
    if (path_.isEmpty() && !listOnly_ && !directory.isEmpty())
        path_ = QDir(directory).filePath(QStringLiteral("selftest-%1.json").arg(QCoreApplication::applicationPid()));
    if (listOnly_ && path_.isEmpty()) error_ = QStringLiteral("Missing inventory output path");
    if (!path_.isEmpty()) {
        QSaveFile probe(path_);
        if (!probe.open(QIODevice::WriteOnly)) error_ = QStringLiteral("Cannot write UI report: ") + path_;
        probe.cancelWriting();
    }
    QSet<QString> names;
    for (const auto& item : cases) {
        const QString name = QString::fromUtf8(item.name);
        if (name.isEmpty() || names.contains(name)) error_ = QStringLiteral("Empty or duplicate UI case name: ") + name;
        names.insert(name);
        inventory_.append(QJsonObject{{QStringLiteral("name"), name},
            {QStringLiteral("suite"), QString::fromUtf8(item.suite ? item.suite : "Ungrouped")}});
        selected_.append(name);
    }
    const QString selection = After(arguments, QStringLiteral("--self-test-cases"));
    const bool all = arguments.contains(QStringLiteral("--self-test-all"));
    if (!selection.isEmpty() && all) error_ = QStringLiteral("Full and exact selection are mutually exclusive");
    if (!listOnly_ && !selection.isEmpty()) {
        QFile file(selection);
        if (!file.open(QIODevice::ReadOnly)) error_ = QStringLiteral("Cannot read UI selection");
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !document.isArray()) error_ = QStringLiteral("Invalid UI selection JSON");
        QSet<QString> requested;
        for (const auto value : document.array()) {
            const auto name = value.toString();
            if (!value.isString() || !names.contains(name) || requested.contains(name))
                error_ = QStringLiteral("Unknown or duplicate UI selector: ") + name;
            requested.insert(name);
        }
        for (int i = selected_.size() - 1; i >= 0; --i)
            if (!requested.contains(selected_[i])) selected_.removeAt(i);
    } else if (!listOnly_ && !all) {
        const QString filter = qEnvironmentVariable("KACHACAD_SELF_TEST_FILTER");
        if (!filter.isEmpty()) {
            for (int i = selected_.size() - 1; i >= 0; --i)
                if (!selected_[i].contains(filter)) selected_.removeAt(i);
        }
    }
    if (selected_.isEmpty()) error_ = QStringLiteral("No UI cases selected");
}

bool Reporting::Contains(const char* name) const
{
    return selected_.contains(QString::fromUtf8(name));
}

void Reporting::Add(const char* name, bool passed, double milliseconds, const QString& started)
{
    if (!passed) ++failed_;
    results_.append(QJsonObject{{QStringLiteral("name"), QString::fromUtf8(name)},
        {QStringLiteral("status"), passed ? QStringLiteral("passed") : QStringLiteral("failed")},
        {QStringLiteral("duration_ms"), milliseconds}, {QStringLiteral("start"), started},
        {QStringLiteral("end"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
}

bool Reporting::Save() const
{
    if (path_.isEmpty()) return Valid();
    QJsonObject object{{QStringLiteral("schema"), 1}, {QStringLiteral("inventory"), inventory_},
        {QStringLiteral("selected"), QJsonArray::fromStringList(selected_)}, {QStringLiteral("cases"), results_},
        {QStringLiteral("total"), results_.size()}, {QStringLiteral("failed"), failed_},
        {QStringLiteral("error"), error_}};
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(object).toJson();
    return file.write(bytes) == bytes.size() && file.commit();
}
}
