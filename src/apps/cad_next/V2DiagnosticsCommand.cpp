//! 「診断情報をコピー」(DIAGNOSTICS_FEATURE_SPEC.md、USER-UI-001 の診断用)。
//!
//! 不具合を見つけた瞬間の状態を、短い文にして持ち出せるようにする。
//! 貼り付ける先は人であったり、別の AI であったりする。
//!
//! ここでやるのは、画面が持っている値を集めて core の入れ物へ詰めることだけ。
//! 何をどう並べるか、何を出してはいけないかは core(app/DiagnosticReport)が決める。

#include "V2MainWindow.h"

#include "V2DrawingDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/DiagnosticReport.h"
#include "kachakacha/app/DrawingShelfRows.h"
#include "kachakacha/app/PointerCursor.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QString>

#include <string>

namespace {

[[nodiscard]] std::string Utf8(const QString& text)
{
    return text.toStdString();
}

//! 組み立てたときに埋め込んだ値。無ければ空。
[[nodiscard]] std::string BuiltInValue(const char* value)
{
    return value == nullptr ? std::string() : std::string(value);
}

} // namespace

kachakacha::v2::app::DiagnosticSnapshot V2MainWindow::DiagnosticSnapshotNow() const
{
    using kachakacha::v2::app::DiagnosticSnapshot;
    DiagnosticSnapshot snapshot;

    snapshot.timestamp = Utf8(QDateTime::currentDateTime().toString(Qt::ISODate));
#ifdef KACHACAD_APP_VERSION
    snapshot.version = BuiltInValue(KACHACAD_APP_VERSION);
#endif
#ifdef KACHACAD_GIT_COMMIT
    snapshot.commit = BuiltInValue(KACHACAD_GIT_COMMIT);
#endif
#ifdef KACHACAD_GIT_BRANCH
    snapshot.branch = BuiltInValue(KACHACAD_GIT_BRANCH);
#endif
    // dirty は組み立てのときにしか分からず、いま汚れているかとは別物である。
    // 別物を同じ名前で出すと嘘になるので、分からないままにしておく。
    snapshot.dirtyKnown = false;

    // 道の並びは core が落とすが、ここでも渡すのは名前だけにしておく。
    snapshot.documentName = Utf8(documentPath_);
    snapshot.activePart = Utf8(ActiveGroupText());

    // 作業平面の名前は、文書に載っているものを使う。
    if (!activeWorkPlaneId_.IsNil()) {
        const auto* entity = session_->GetDocument().FindEntity(activeWorkPlaneId_);
        if (entity != nullptr) {
            snapshot.activeWorkPlane = entity->displayName;
        }
    }

    // そろっているべき5つ。**別々に読む。** 同じ場所から作ると、
    // ずれていても同じ値になってしまい、ずれを見つけられない。
    const auto tool = session_->CurrentTool();
    snapshot.activeTool = std::string(
        kachakacha::v2::modeling::DrawingToolNameJa(tool));
    snapshot.rightPanelMode = std::string(kachakacha::v2::app::ShelfNameJa(
        kachakacha::v2::app::FrontShelfFor(mode_, tool)));
    if (drawingDock_ != nullptr) {
        // 棚が自分で覚えている道具を読む。画面の道具から作り直さない。
        snapshot.rightPanelTool = std::string(
            kachakacha::v2::modeling::DrawingToolNameJa(drawingDock_->Tool()));
    }
    if (viewport_ != nullptr) {
        // カーソルも画面が実際に出している形から読む。
        snapshot.cursorMode = std::string(kachakacha::v2::app::CursorShapeNameJa(
            kachakacha::v2::app::ChooseCursorShape(viewport_->CursorContextNow())));
        // 途中経過を出しているのは、いまの道具のはずである。
        snapshot.previewOwner = viewport_->HasPreview()
            ? std::string(kachakacha::v2::modeling::DrawingToolNameJa(tool))
            : std::string("(なし)");
        const auto& hover = viewport_->Hover();
        if (hover.snap.has_value()) {
            snapshot.snapOwner = std::string(
                kachakacha::v2::modeling::DrawingToolNameJa(tool));
            snapshot.snapType = std::string(
                kachakacha::v2::app::SnapKindNameJa(hover.snap->kind));
            const auto* entity =
                session_->GetDocument().FindEntity(hover.snap->entityId);
            snapshot.snapTarget = entity != nullptr ? entity->displayName
                                                    : std::string("(名前なし)");
        }

        const auto& selection = viewport_->Selection();
        snapshot.selectionCount =
            kachakacha::v2::app::SelectionItemCount(selection);
        for (const auto& id : selection.entityIds) {
            const auto* entity = session_->GetDocument().FindEntity(id);
            snapshot.selection.push_back(
                entity != nullptr && !entity->displayName.empty()
                    ? entity->displayName
                    : std::string("名前のないもの"));
        }
    }
    return snapshot;
}

QString V2MainWindow::DiagnosticText() const
{
    return QString::fromStdString(
        kachakacha::v2::app::FormatDiagnosticReport(DiagnosticSnapshotNow()));
}

void V2MainWindow::CopyDiagnostics()
{
    const QString text = DiagnosticText();
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(text);
        SetStatus(QStringLiteral("診断情報をクリップボードへコピーしました。"));
        return;
    }
    // 貼り板が無い場面(画面を出さない自己試験など)。作れたことだけ言う。
    SetStatus(QStringLiteral("診断情報を作りました(貼り板が使えません)。"));
}
