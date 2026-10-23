#pragma once

#include "kachakacha/app/DrawingSession.h"
#include <QObject>
#include <cstdint>
#include <map>
#include <vector>

class V2Viewport;

// UI-only lifetime: document history stays in Document; pending points stay in DrawingSession.
// This remembers only which pending input prefix existed when an interleaved deletion occurred.
class V2DrawingSelection final : public QObject {
public:
    static V2DrawingSelection& Of(QObject& viewport);
    static bool IsDrawing(kachakacha::v2::modeling::DrawingTool tool);
    static bool CanSelect(const kachakacha::v2::app::DrawingSession&, const V2Viewport&);
    static constexpr const char* DeleteLabel = "選択と参照先を削除";
    void SyncInputs(const kachakacha::v2::app::DrawingSession&);
    void CancelInputs();
    void Select() { selecting_ = true; }
    void ResumeInput() { selecting_ = false; }
    bool SelectionDelete() const { return selecting_; }
    void Deleted(const kachakacha::v2::app::DrawingSession&, std::uint64_t before, bool hadUndo);
    bool PreferDocumentUndo(const kachakacha::v2::app::DrawingSession&);
    void HistoryMoved(const kachakacha::v2::app::DrawingSession&, bool undo, std::uint64_t before);
private:
    explicit V2DrawingSelection(QObject& parent);
    struct Prefix {
        kachakacha::v2::modeling::DrawingTool tool{};
        std::size_t count{};
        std::uint64_t token{};
        bool operator==(const Prefix&) const = default;
    };
    Prefix Current(const kachakacha::v2::app::DrawingSession&) const;
    kachakacha::v2::base::DocumentId documentId_;
    std::vector<std::uint64_t> inputs_;
    std::uint64_t nextToken_ = 0;
    bool selecting_ = false;
    std::map<std::pair<std::uint64_t, std::uint64_t>, Prefix> deletions_;
};
