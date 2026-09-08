#include "kachakacha/app/EvaluationQueue.h"

#include <algorithm>

namespace kachakacha::v2::app {
namespace {

using base::MakeError;
using base::Result;

} // namespace

EvaluationRequest EvaluationQueue::Submit(std::uint64_t documentRevision, FeatureId root,
    double nowMs)
{
    if (current_.has_value()) {
        // 前の評価は用済み。合図を立てて、返ってきても採用しないようにする。
        current_->cancel.Cancel();
        ++cancelled_;
    }
    EvaluationRequest request;
    request.jobId = nextJobId_++;
    request.documentRevision = documentRevision;
    request.root = root;
    request.startedMs = nowMs;
    current_ = request;
    lastHeartbeatMs_ = nowMs;
    return request;
}

void EvaluationQueue::CancelCurrent()
{
    if (!current_.has_value()) {
        return;
    }
    current_->cancel.Cancel();
    ++cancelled_;
    current_.reset();
}

Result<std::uint64_t> EvaluationQueue::Accept(const EvaluationOutcome& outcome)
{
    if (!current_.has_value()) {
        ++stale_;
        return Result<std::uint64_t>::Failure(MakeError("PER-002",
            "遅れて返った古い評価の結果は使いません。",
            "いま待っている評価がありません。"));
    }
    if (outcome.jobId != current_->jobId) {
        ++stale_;
        return Result<std::uint64_t>::Failure(MakeError("PER-002",
            "遅れて返った古い評価の結果は使いません。",
            "待っているのは " + std::to_string(current_->jobId) + " 番で、返ったのは "
                + std::to_string(outcome.jobId) + " 番です。"));
    }
    if (outcome.cancelled || current_->cancel.IsCancelled()) {
        ++stale_;
        return Result<std::uint64_t>::Failure(MakeError("PER-001",
            "評価を途中でやめました。", "取り消された評価の結果は使いません。"));
    }
    if (outcome.documentRevision != current_->documentRevision) {
        ++stale_;
        return Result<std::uint64_t>::Failure(MakeError("PER-002",
            "遅れて返った古い評価の結果は使いません。",
            "文書は " + std::to_string(current_->documentRevision) + " 版で、結果は "
                + std::to_string(outcome.documentRevision) + " 版のものです。"));
    }
    accepted_ = outcome.documentRevision;
    current_.reset();
    return Result<std::uint64_t>::Success(accepted_);
}

void EvaluationQueue::Heartbeat(double nowMs)
{
    lastHeartbeatMs_ = std::max(lastHeartbeatMs_, nowMs);
}

std::uint64_t EvaluationQueue::CurrentJobId() const noexcept
{
    return current_.has_value() ? current_->jobId : 0;
}

double EvaluationQueue::ElapsedMs(double nowMs) const
{
    if (!current_.has_value()) {
        return 0.0;
    }
    return std::max(0.0, nowMs - current_->startedMs);
}

bool EvaluationQueue::IsResponsive(double nowMs) const
{
    if (!current_.has_value()) {
        return true;
    }
    return nowMs - lastHeartbeatMs_ <= kHeartbeatIntervalMs;
}

bool EvaluationQueue::ShouldOfferCancel(double nowMs) const
{
    return current_.has_value() && ElapsedMs(nowMs) > kSlowEvaluationMs;
}

} // namespace kachakacha::v2::app
