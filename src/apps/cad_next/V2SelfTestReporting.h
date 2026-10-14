#pragma once

#include "V2SelfTest.h"
#include <QJsonArray>
#include <QString>
#include <QStringList>

namespace kachakacha::v2::selftest {
class Reporting {
public:
    explicit Reporting(const std::vector<SelfTestCase>& cases);
    [[nodiscard]] bool Valid() const noexcept { return error_.isEmpty(); }
    [[nodiscard]] bool Contains(const char* name) const;
    [[nodiscard]] bool ListOnly() const noexcept { return listOnly_; }
    void Add(const char* name, bool passed, double milliseconds, const QString& started);
    [[nodiscard]] bool Save() const;
    [[nodiscard]] QString Error() const { return error_; }
private:
    QJsonArray inventory_;
    QJsonArray results_;
    QStringList selected_;
    QString path_;
    QString error_;
    bool listOnly_ = false;
    int failed_ = 0;
};

[[nodiscard]] std::vector<SelfTestCase> WithSuite(std::vector<SelfTestCase> cases, const char* suite);
}
