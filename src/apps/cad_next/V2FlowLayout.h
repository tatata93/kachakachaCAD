#pragma once
#include <QLayout>
#include <QSize>
#include <QRect>
#include <vector>
class QWidget;
class V2FlowLayout final : public QLayout {
public:
    explicit V2FlowLayout(QWidget* parent):QLayout(parent) {}
    ~V2FlowLayout() override;
    void addItem(QLayoutItem* item) override;
    void insertWidget(int index,QWidget* widget);
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    Qt::Orientations expandingDirections() const override {return {};}
    bool hasHeightForWidth() const override {return true;}
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
private:
    int Arrange(const QRect& rect,bool measure) const;
    std::vector<QLayoutItem*> items_;
};
