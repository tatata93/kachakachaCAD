#pragma once
#include <QWidget>
#include <string_view>
#include <string>
#include "kachakacha/app/Selection.h"
class V2MainWindow;
class QLabel;
class QEvent;
class QObject;
class V2FaceActions final : public QWidget {
public:
    static bool Run(V2MainWindow& window, std::string_view command);
private:
    V2FaceActions(V2MainWindow& window, std::string command);
    bool eventFilter(QObject* object, QEvent* event) override;
    void Apply(const kachakacha::v2::app::SelectionRef& face);
    bool Extract(const kachakacha::v2::app::SelectionRef& face);
    V2MainWindow& window_;
    std::string command_;
    QLabel* status_ = nullptr;
};
