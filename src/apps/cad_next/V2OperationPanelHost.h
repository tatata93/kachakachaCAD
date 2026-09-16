#pragma once

#include "kachakacha/app/ShelfLayout.h"

#include <QWidget>

#include <map>
#include <vector>

class QComboBox;
class QLabel;
class QStackedWidget;

//! 右側に「現在の操作」だけを表示する入れ物。
//!
//! 各操作パネルの実装はそのまま利用し、独立した Dock の束をユーザーへ
//! 見せない。関連する設定が複数あるときだけ、上部の選択欄から切り替える。
class V2OperationPanelHost final : public QWidget {
public:
    explicit V2OperationPanelHost(QWidget* parent = nullptr);

    void AddPage(kachakacha::v2::app::Shelf shelf, QWidget* page);
    void SetShelves(const std::vector<kachakacha::v2::app::Shelf>& shelves);

    [[nodiscard]] bool Shows(kachakacha::v2::app::Shelf shelf) const;
    [[nodiscard]] kachakacha::v2::app::Shelf CurrentShelf() const noexcept;

private:
    void ActivateIndex(int index);

    QLabel* title_ = nullptr;
    QComboBox* pageChoice_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::map<kachakacha::v2::app::Shelf, QWidget*> pageByShelf_;
    std::vector<kachakacha::v2::app::Shelf> shownShelves_;
    kachakacha::v2::app::Shelf current_ = kachakacha::v2::app::Shelf::None;
};
