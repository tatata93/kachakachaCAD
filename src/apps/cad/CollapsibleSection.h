#pragma once

#include <QString>
#include <QWidget>

class QToolButton;

//! 見出しクリックで開閉するセクション(ADR 0020 第1段)。
//! 長い縦一列フォームを役割ごとに畳み、右パネルの常時スクロールをなくす。
//! moc は使わない(既存方針)。
class CollapsibleSection : public QWidget {
public:
    explicit CollapsibleSection(
        const QString& title,
        QWidget* content,
        bool expanded = true,
        QWidget* parent = nullptr);

    void SetExpanded(bool expanded);
    [[nodiscard]] bool IsExpanded() const;
    [[nodiscard]] QWidget* Content() const noexcept { return content_; }
    [[nodiscard]] const QString& Title() const noexcept { return title_; }

    //! widget の先祖にある折りたたみセクションをすべて開く
    //! (セルフテスト・マニュアル画像がアンカーへスクロールする前に使う)。
    static void ExpandAncestors(QWidget* widget);

private:
    QString title_;
    QToolButton* header_ = nullptr;
    QWidget* content_ = nullptr;
};
