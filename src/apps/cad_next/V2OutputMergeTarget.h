#pragma once
#include "kachakacha/io/DocumentFile.h"
#include <QWidget>
#include <functional>
#include <optional>
class QLabel;

// A read-only destination snapshot. Neither source document is adopted or edited.
class V2OutputMergeTarget final:public QWidget {
public:
    V2OutputMergeTarget(QWidget* parent,std::function<QString()> choose,std::function<void()> changed);
    bool Ready() const{return file_.has_value();}
    QString Description() const;
    bool CheckUnchanged(QString& error) const;
    const kachakacha::v2::io::DocumentFile& File() const{return *file_;}
    const QString& Path() const{return path_;}
private:
    void Choose();
    std::function<QString()> choose_;
    std::function<void()> changed_;
    QLabel* status_=nullptr;
    QString path_;
    std::string archive_;
    std::optional<kachakacha::v2::io::DocumentFile> file_;
};
