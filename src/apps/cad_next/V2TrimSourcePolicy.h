#pragma once
#include "kachakacha/document/Document.h"
#include "kachakacha/app/DrawingSession.h"
#include <QString>
#include <functional>
#include <memory>
#include <vector>
class QCheckBox;
class QWidget;
class V2MainWindow;
namespace v2ui {
// Compound document history and its immediate drawing scene must roll back together.
class TrimGenerationTransaction final {
public:
    TrimGenerationTransaction(kachakacha::v2::app::DrawingSession& session, const std::string& label);
    ~TrimGenerationTransaction();
    bool Commit();
    void Rollback();
private:
    kachakacha::v2::app::DrawingSession& session_;
    kachakacha::v2::modeling::SnapScene scene_;
    std::unique_ptr<kachakacha::v2::document::Document::Transaction> transaction_;
    bool committed_ = false;
    bool rolledBack_ = false;
};
QCheckBox* MakeTrimKeepOriginal(QWidget* parent, const char* name);
bool TrimKeepsOriginal(const QWidget& owner, const char* name);
struct TrimSourceRemoval {
    bool accepted = true;
    bool reusesSource = false;
    bool rebuildShapes = false;
    QString message;
    std::unique_ptr<kachakacha::v2::document::DocumentCommand> command;
};
TrimSourceRemoval PrepareTrimSourceReplacement(V2MainWindow& window,
    kachakacha::v2::base::EntityId source,
    const std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>>& chains,
    bool keep, kachakacha::v2::base::IdGenerator& ids, const std::function<void()>& refreshShapes);
// Prepare/confirm before opening the generation transaction. Apply the command
// inside that transaction, then create independent results. No hidden fallback.
TrimSourceRemoval PrepareTrimSourceRemoval(V2MainWindow& window,
    const std::vector<kachakacha::v2::base::EntityId>& sources, bool keep,
    const std::function<void()>& refreshShapes);
}
