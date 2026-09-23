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
    //! 正対できる相手が1つ以上。作業平面・立体の面・立体・形状ガイド・線・点。
    //! 「平らな面か作業平面」だけでは、立体の面も曲がった面も選べなかった。
    AnythingToFace,
    ZeroOrOneGroup,
    OneOrMoreWires,
    //! ワイヤーを2つ以上。交点に点を作るときに使う。
    TwoOrMoreWires,
    TwoWireChains,
    OneClosedProfile,
    //! 閉じた輪郭が1つ以上、または立体1つとその面。押し出しと押し引きの入口。
    //! 「閉じた輪郭が1つ以上」だけの条件は、これに畳んだ(使い手が居なくなったため)。
    ClosedProfilesOrSolidFace,
    OnePart,
    //! 部品を2つ以上。足す・引く(土台 1 つと相手 1 個以上。相手は何個でも)。
    TwoOrMoreParts,
    OneDerivedEntity,
    //! 作られたもの(派生)を1つ以上。現在状態を固定(選んだものごとに 1 本ずつ固定する)。
    OneOrMoreDerivedEntities,
    OneFabricationModel,
    OneFabricationPanel,
    OneOrMorePatterns,
    OneOrMoreSelectedCurves,
    //! ワイヤーと、落とす先の形状ガイドの面 2 枚以上。回り込み投影に使う。
    WiresAndTwoOrMoreGuideSurfaces,
    //! 部品1つ、または形状ガイド1つ。製作はどちらからでも始められる。
    OnePartOrSurface,
    //! 部品を1つ以上。書き出し・出力の検査(何個でもまとめて出す・調べる)。
    OneOrMoreParts,
    //! 部品か形状ガイドを1つ以上。製作モデルを作る(元は何個でも 1 つのモデルにまとめる)。
    OneOrMorePartsOrSurfaces,
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
    //! 形状ガイドの面1つ以上と作業平面ちょうど1つ。面を平面まで立体に(面ごとに 1 部品)。
    GuideSurfacesAndOneWorkPlane,
    //! 画面から隠せるもの(線・部品・形状ガイドの面)を1つ以上。
    //! 立体と面が画面に出るようになったので、線だけでは足りない。
    //! 隠せないと、見たくない部品が画面に居座る。
    OneOrMoreHideable,
    //! 消せるものを 1 つ以上(線・面・作業平面・部品・近似モデル・生成物…作られたもの全部)。
    //! 「ワイヤーだけ」にしていたので、面や作業平面や近似モデルを選んでも削除が押せなかった
    //! (オーナー指摘 2026-09-24)。原点の平面や使われているものは、押したあとに文書が理由を言う。
    OneOrMoreDeletable,
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
