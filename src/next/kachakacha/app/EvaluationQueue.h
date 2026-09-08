#pragma once

//! 評価の積み方(architecture-and-data.md §9、AT-PER-001 / 002)。
//!
//! 100ms を超えるかもしれない評価は、不変の断面を持たせて別の場所へ渡す。
//! そのあいだに文書が変われば、前の評価は取り消し、遅れて返った結果は採用しない。
//!
//! ここには「いつ、どの結果を採用してよいか」の判断だけを置く。
//! 実際に計算する場所(OCCT側、あるいはワーカースレッド)は知らない。
//! そうしないと、この判断を試験するのに OCCT とスレッドが要ることになる。
//!
//! 時刻は呼び出し側が渡す。中で時計を読むと、試験が時間に依存して揺れる。
//!
//! V1 は評価が同期で、長い近似のあいだ画面が固まった。途中で文書を変えると、
//! 古い結果が後から上書きしてくることもあった。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kachakacha::v2::app {

using base::FeatureId;

//! 取り消しの合図。評価する側はこれを見て、途中でやめる。
class CancellationToken {
public:
    CancellationToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

    [[nodiscard]] bool IsCancelled() const noexcept { return flag_->load(); }
    void Cancel() const noexcept { flag_->store(true); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

//! 積んだ評価1件。
struct EvaluationRequest {
    std::uint64_t jobId = 0;
    std::uint64_t documentRevision = 0;
    FeatureId root;
    CancellationToken cancel;
    double startedMs = 0.0;
};

//! 返ってきた結果。中身(三角形など)は呼び出し側の型なので、ここでは持たない。
struct EvaluationOutcome {
    std::uint64_t jobId = 0;
    std::uint64_t documentRevision = 0;
    bool cancelled = false;
};

//! 評価が「遅い」と見なす時間。これを超えたら取消ボタンを出す。
inline constexpr double kSlowEvaluationMs = 100.0;
//! 心拍がこの間隔以内で更新されていれば、画面は応答している。
inline constexpr double kHeartbeatIntervalMs = 250.0;

class EvaluationQueue {
public:
    //! 新しい評価を積む。走っている評価があれば、そちらは取り消す。
    [[nodiscard]] EvaluationRequest Submit(std::uint64_t documentRevision, FeatureId root,
        double nowMs);

    //! 利用者が押した取消。
    void CancelCurrent();

    //! 返ってきた結果を採用してよいか。よくない理由は診断で返す。
    [[nodiscard]] base::Result<std::uint64_t> Accept(const EvaluationOutcome& outcome);

    //! 心拍を打つ。評価する側が刻む。
    void Heartbeat(double nowMs);

    //! いま走っているか。
    [[nodiscard]] bool IsRunning() const noexcept { return current_.has_value(); }
    //! 走っている評価の番号。
    [[nodiscard]] std::uint64_t CurrentJobId() const noexcept;
    //! 最後に採用した文書の版。
    [[nodiscard]] std::uint64_t AcceptedRevision() const noexcept { return accepted_; }
    //! 取り消して捨てた件数と、古くて捨てた件数。試験と画面の両方が見る。
    [[nodiscard]] std::uint64_t CancelledCount() const noexcept { return cancelled_; }
    [[nodiscard]] std::uint64_t DiscardedStaleCount() const noexcept { return stale_; }

    //! 心拍が間に合っているか。走っていなければ常に真。
    [[nodiscard]] bool IsResponsive(double nowMs) const;
    //! 取消ボタンを出すか。100ms を超えてから出す。
    [[nodiscard]] bool ShouldOfferCancel(double nowMs) const;
    //! 走り始めてからの時間(ms)。走っていなければ0。
    [[nodiscard]] double ElapsedMs(double nowMs) const;

private:
    std::optional<EvaluationRequest> current_;
    std::uint64_t nextJobId_ = 1;
    std::uint64_t accepted_ = 0;
    std::uint64_t cancelled_ = 0;
    std::uint64_t stale_ = 0;
    double lastHeartbeatMs_ = 0.0;
};

} // namespace kachakacha::v2::app
