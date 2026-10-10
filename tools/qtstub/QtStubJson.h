#pragma once
//! JSON保存境界の型検査だけに使う宣言。値の実装・解析は行わない。
#include "QtStubCore.h"
class QJsonObject;
class QJsonArray;
class QJsonValue {
public:
    QJsonValue() = default;
    QJsonValue(const char*) {}
    QJsonValue(const QString&) {}
    QJsonValue(bool) {}
    QJsonValue(int) {}
    QJsonValue(double) {}
    QJsonValue(const QJsonObject&) {}
    QJsonValue(const QJsonArray&) {}
    bool isArray() const;bool isObject() const;bool isString() const;bool isBool() const;
    bool isDouble() const;bool isUndefined() const;
    int toInt(int = 0) const;double toDouble(double = 0) const;bool toBool(bool = false) const;
    QString toString(const QString& = QString()) const;
    QJsonObject toObject() const;QJsonArray toArray() const;
    bool operator==(const QJsonValue&) const;bool operator!=(const QJsonValue&) const;
};
class QJsonArray : public std::vector<QJsonValue> {
public:
    using std::vector<QJsonValue>::vector;
    bool isEmpty() const;int size() const;
    const QJsonValue& last() const;QJsonValue& last();
    QJsonValue& operator[](int);const QJsonValue& operator[](int) const;
    bool operator==(const QJsonArray&) const;bool operator!=(const QJsonArray&) const;
};
class QJsonObject {
public:
    QJsonObject() = default;
    QJsonObject(std::initializer_list<std::pair<QString,QJsonValue>>) {}
    QJsonValue& operator[](const QString&);QJsonValue operator[](const QString&) const;
    bool isEmpty() const;bool contains(const QString&) const;void remove(const QString&);
    bool operator==(const QJsonObject&) const;bool operator!=(const QJsonObject&) const;
};
class QJsonParseError {
public:
    enum ParseError { NoError };
    ParseError error=NoError;
};
class QJsonDocument {
public:
    enum JsonFormat { Indented,Compact };
    QJsonDocument() = default;QJsonDocument(const QJsonObject&) {}
    static QJsonDocument fromJson(const QByteArray&,QJsonParseError* = nullptr);
    QJsonObject object() const;QByteArray toJson(JsonFormat = Indented) const;
    bool isObject() const;
};
