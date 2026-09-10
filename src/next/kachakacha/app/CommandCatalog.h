#pragma once

//! コマンド台帳(command-catalog.md、AT-UIX-011)。
//!
//! メニュー、道具箱、右パネル、ショートカットは、すべてここの1件を指す。
//! 同じ処理へ別のIDや別の入口を作ってはならない。
//! V1 は同じ操作がメニューとボタンで別々に書かれていて、
//! 片方だけ直したせいで挙動が食い違った。
//!
//! この表と `docs/v2/command-catalog.md` が食い違うと
//! `tests_v2/command_catalog_tests.cpp` が失敗する。

#include <cstddef>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! コマンドの出方。
enum class CommandMode {
    Instant,   //!< 押したらすぐ効く
    Tool,      //!< 道具に入り、画面で点を置く
    Dialog,    //!< 窓を開いて設定してから効く
    Modeless,  //!< 開いたままにできる窓
};

//! 使える条件。満たさないときは隠さず、押せなくして理由を出す。
enum class SelectionPredicate {
    Always,
    HasDocument,
    HasUndo,
    HasRedo,
    HasVisibleGeometry,
    OneWorkPlane,
    OnePlanarFaceOrWorkPlane,
    ZeroOrOneGroup,
    OneOrMoreWires,
    TwoWireChains,
    OneClosedProfile,
    OneOrMoreClosedProfiles,
    OnePart,
    TwoParts,
    OneDerivedEntity,
    OneFabricationModel,
    OneFabricationPanel,
    OneOrMorePatterns,
    OneOrMoreSelectedCurves,
    //! 部品1つ、または形状ガイド1つ。製作はどちらからでも始められる。
    OnePartOrSurface,
    //! 形状ガイドの面を1つ以上。厚みを付けて立体にするときに使う。
    OneOrMoreGuideSurfaces,
    //! ワイヤーか形状ガイドの面を1つ以上。役割表へ入れるときに使う(離した面は面を指す)。
    OneOrMoreWiresOrGuideSurfaces,
    //! 役割表で行を1つ選んでいる。行を動かす・消す・反転する・線を足すときに使う。
    OneGuideRow,
    //! 役割表に行が1つ以上ある。表から面を作る・表を空にするときに使う。
    OneOrMoreGuideRows,
    //! ワイヤー1つ以上と形状ガイドの面ちょうど1つ。線を曲面へ落とすときに使う。
    WiresAndOneGuideSurface,
    //! 形状ガイドの面ちょうど1つと作業平面ちょうど1つ。面を平面まで立体にするときに使う。
    OneGuideSurfaceAndOneWorkPlane,
};

[[nodiscard]] std::string_view SelectionPredicateNameJa(SelectionPredicate value) noexcept;

struct CommandDescriptor {
    std::string_view id;
    std::string_view labelJa;
    CommandMode mode = CommandMode::Instant;
    //! 記号の名前。絵そのものは画面側が持つ。空にしてはならない。
    std::string_view icon;
    //! 既定のショートカット。無いものは空でよい。
    std::string_view defaultShortcut;
    SelectionPredicate predicate = SelectionPredicate::Always;
    //! 条件を満たさないときに出す一文。
    std::string_view predicateFailureJa;
    //! 何をする操作かの案内。画面の下に出す。
    std::string_view operationGuideJa;
    //! 文書を変えるか。変えるなら DocumentCommand を1つ返すこと。
    bool changesDocument = false;
    //! 対応する受入試験。1件以上。
    std::vector<std::string_view> acceptanceIds;
};

//! 台帳。IDの順は決定的(台帳の並び順)。
[[nodiscard]] const std::vector<CommandDescriptor>& CommandCatalog();

//! IDで引く。無ければ nullptr。
[[nodiscard]] const CommandDescriptor* FindCommand(std::string_view id);

} // namespace kachakacha::v2::app
