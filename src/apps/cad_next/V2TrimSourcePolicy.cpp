#include "V2TrimSourcePolicy.h"
#include "V2DeletionConfirmation.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/document/Commands.h"
#include <QCheckBox>
#include <QStringList>
#include <QString>
#include <QWidget>
#include <algorithm>

namespace {
using namespace kachakacha::v2;
class TrimRemoval final : public document::DocumentCommand {
public:
    explicit TrimRemoval(document::DocumentSnapshot after) : after_(std::move(after)) {}
    std::string Label() const override { return "トリム前の線を削除"; }
    std::vector<base::Diagnostic> Apply(document::DocumentSnapshot& current) const override {
        if (current.id != after_.id || current.revision != after_.revision)
            return {base::MakeError("DOC-C001", "削除の確認中に文書が変わりました。やり直してください。")};
        current = after_;
        return {};
    }
private:
    document::DocumentSnapshot after_;
};
}

v2ui::TrimGenerationTransaction::TrimGenerationTransaction(app::DrawingSession& session, const std::string& label)
    : session_(session), scene_(session.Scene()),
      transaction_(std::make_unique<document::Document::Transaction>(session.GetDocument(), label)) {}

v2ui::TrimGenerationTransaction::~TrimGenerationTransaction()
{
    transaction_.reset(); // Restore the document before restoring its drawing scene.
    if (!committed_) session_.SetScene(std::move(scene_));
}

bool v2ui::TrimGenerationTransaction::Commit()
{
    committed_ = transaction_->Commit();
    return committed_;
}

QCheckBox* v2ui::MakeTrimKeepOriginal(QWidget* parent, const char* name)
{
    auto* check = new QCheckBox(QStringLiteral("元の線を残す"), parent);
    check->setObjectName(QString::fromLatin1(name));
    check->setChecked(false);
    check->setToolTip(QStringLiteral("オン: 変更前の線を表示したまま残し、結果を別ワイヤーにします。\nオフ: 元の線を削除します。使っている面・寸法なども消える場合は、一覧と3D表示で確認します。"));
    return check;
}

bool v2ui::TrimKeepsOriginal(const QWidget& owner, const char* name)
{
    const auto* check = owner.findChild<QCheckBox*>(QString::fromLatin1(name));
    return check && check->isChecked();
}

v2ui::TrimSourceRemoval v2ui::PrepareTrimSourceRemoval(V2MainWindow& window,
    const std::vector<base::EntityId>& sources, bool keep,
    const std::function<void()>& refreshShapes)
{
    TrimSourceRemoval result;
    if (keep || sources.empty()) return result;
    const auto before = window.Session().GetDocument().Snapshot();
    auto after = before;
    for (const auto id : sources) {
        const auto found = std::find_if(after.entities.begin(), after.entities.end(), [id](const auto& e) { return e.id == id; });
        if (found == after.entities.end()) continue;
        const auto diagnostics = document::RemoveFeatureCommand(found->createdBy, document::RemovePolicy::Cascade, "トリム前の線を削除").Apply(after);
        for (const auto& d : diagnostics) if (d.IsError()) {
            result.accepted = false; result.message = QString::fromStdString(d.summaryJa); return result;
        }
    }
    const auto gone = [&](base::EntityId id) {
        return std::none_of(after.entities.begin(), after.entities.end(), [id](const auto& e) { return e.id == id; });
    };
    QStringList names;
    std::vector<base::EntityId> preview;
    bool dependent = false;
    for (const auto& entity : before.entities) if (gone(entity.id)) {
        names.push_back(QString::fromStdString(entity.displayName) + " [" + QString::fromStdString(entity.id.ShortSuffix()) + "]");
        preview.push_back(entity.id);
        if (std::find(sources.begin(), sources.end(), entity.id) == sources.end()) dependent = true;
    }
    std::erase_if(after.referenceDimensions, [&](const auto& d) {
        const bool affected = std::any_of(d.targets.begin(), d.targets.end(), gone)
            || std::any_of(d.segments.begin(), d.segments.end(), [&](const auto& s) { return gone(s.entityId); });
        if (affected) { names.push_back(QStringLiteral("寸法: ") + QString::fromStdString(d.label)); dependent = true; }
        return affected;
    });
    if (dependent) {
        window.Viewport().SetDeletionPreview(preview); refreshShapes();
        const int choice = ConfirmDeletion(&window, names);
        window.Viewport().SetDeletionPreview({}); refreshShapes();
        if (choice < 0) {
            result.accepted = false;
            result.message = QStringLiteral("削除を取り消しました。トリム前の線と結果は変更していません。");
            return result;
        }
    }
    if (window.Session().GetDocument().Revision() != before.revision) {
        result.accepted = false;
        result.message = QStringLiteral("確認中に文書が変わりました。トリムをやり直してください。");
        return result;
    }
    result.command = std::make_unique<TrimRemoval>(std::move(after));
    return result;
}
