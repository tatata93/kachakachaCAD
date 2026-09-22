#pragma once

#include "kachakacha/app/ShelfLayout.h"

#include <QString>
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
    //! 見出しの下の一行(いまの案内)。空なら隠す。文言は窓が core から持ってくる。
    void SetHint(const QString& hintJa);
    [[nodiscard]] QString HintText() const;

    //! いま出している棚(試験から: 節の並びとキャンセル・確定を読む、C-10)。無ければ nullptr。
    [[nodiscard]] QWidget* CurrentPage() const;
    //! 見出しの字(棚の名前。棚が無ければ「現在の操作」)。
    [[nodiscard]] QString CurrentShelfTitle() const;
    [[nodiscard]] bool Shows(kachakacha::v2::app::Shelf shelf) const;
    [[nodiscard]] kachakacha::v2::app::Shelf CurrentShelf() const noexcept;

private:
    void ActivateIndex(int index);

    QLabel* title_ = nullptr;
    QLabel* hint_ = nullptr;
    QComboBox* pageChoice_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::map<kachakacha::v2::app::Shelf, QWidget*> pageByShelf_;
    std::vector<kachakacha::v2::app::Shelf> shownShelves_;
    kachakacha::v2::app::Shelf current_ = kachakacha::v2::app::Shelf::None;
};
