#pragma once

#include <QWidget>
#include <vector>

class QAction;
class QHBoxLayout;
class QToolButton;

// A single toolbar row with the same actions in buttons and in its overflow menu.
class V2ToolbarRow final : public QWidget {
public:
    V2ToolbarRow(const QString& overflowLabel, QWidget* parent);
    void Add(QWidget* widget, QAction* action);
    void Clear();
    void Relayout();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void PopulateMenu();
    static int WidthOf(const QWidget& widget);

    struct Entry {
        QWidget* widget;
        QAction* action;
    };
    std::vector<Entry> entries_;
    QHBoxLayout* row_;
    QToolButton* more_;
    std::size_t shown_ = 0;
};
