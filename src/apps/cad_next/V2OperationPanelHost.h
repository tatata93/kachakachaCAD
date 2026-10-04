#pragma once

#include "kachakacha/app/ShelfLayout.h"

#include <QString>
#include <QPointer>
#include <QWidget>

#include "kachakacha/app/Ribbon.h"
#include <functional>
#include <optional>
#include <map>
#include <vector>

class QAction;
class QLineEdit;
class QCheckBox;
class QGroupBox;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;

//! 右側に「現在の操作」だけを表示する入れ物。
//!
//! 各操作パネルの実装はそのまま利用し、独立した Dock の束をユーザーへ
//! 見せない。関連する設定が複数あるときだけ、上部の選択欄から切り替える。
class V2OperationPanelHost final : public QWidget {
public:
    explicit V2OperationPanelHost(QWidget* parent = nullptr);

    void SetToolHandler(std::function<void(kachakacha::v2::app::UiMode,const kachakacha::v2::app::RibbonTool&)> handler);
    void SetToolMode(kachakacha::v2::app::UiMode mode);
    void FocusToolSearch();
    void SetActionLookup(std::function<QAction*(std::string_view)> lookup);
    void AddPage(kachakacha::v2::app::Shelf shelf, QWidget* page);
    void SetShelves(const std::vector<kachakacha::v2::app::Shelf>& shelves);
    //! 見出しの下の一行(いまの案内)。空なら隠す。文言は窓が core から持ってくる。
    void ShowTemporaryPage(QWidget* page, const QString& title);
    void ClearTemporaryPage();
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
    void FilterTools();
    QLineEdit* search_=nullptr;
    QCheckBox* allModes_=nullptr;
    QLabel* searchCount_=nullptr;
    struct SearchEntry { kachakacha::v2::app::UiMode mode; QGroupBox* group; QPushButton* button; QString text; };
    std::vector<SearchEntry> searchEntries_;
    std::function<QAction*(std::string_view)> actionLookup_;

    QPointer<QWidget> temporary_;
    QPushButton* back_ = nullptr;
    QWidget* chooser_ = nullptr;
    std::optional<kachakacha::v2::app::UiMode> chooserMode_;
    std::function<void(kachakacha::v2::app::UiMode,const kachakacha::v2::app::RibbonTool&)> toolHandler_;
    QLabel* title_ = nullptr;
    QLabel* hint_ = nullptr;
    QComboBox* pageChoice_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::map<kachakacha::v2::app::Shelf, QWidget*> pageByShelf_;
    std::vector<kachakacha::v2::app::Shelf> shownShelves_;
    kachakacha::v2::app::Shelf current_ = kachakacha::v2::app::Shelf::None;
};
