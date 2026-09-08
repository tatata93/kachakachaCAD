// 評価の積み方と古い結果の破棄(AT-PER-001 / AT-PER-002)。
#include "kachakacha/app/EvaluationQueue.h"
#include "kachakacha/base/TestHarness.h"

#include <string>
#include <thread>
#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::base::FeatureId;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;
using namespace kachakacha::v2::app;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] FeatureId Root(std::uint64_t value)
{
    kachakacha::v2::base::DeterministicIdGenerator generator(value);
    return FeatureId(generator.Next());
}

} // namespace

KACHA_V2_TEST(evaluation, 積んだ評価は走っている)
{
    EvaluationQueue queue;
    Require(!queue.IsRunning(), "はじめは走っていない");
    const EvaluationRequest request = queue.Submit(7, Root(1), 0.0);
    Require(queue.IsRunning(), "走っている");
    RequireEqual(std::to_string(queue.CurrentJobId()), std::to_string(request.jobId),
        "同じ番号");
    RequireEqual(std::to_string(request.documentRevision), "7", "文書の版を持つ");
    Require(!request.cancel.IsCancelled(), "取り消されていない");
}

KACHA_V2_TEST(evaluation, 番号は使い回さない)
{
    EvaluationQueue queue;
    const auto first = queue.Submit(1, Root(1), 0.0);
    const auto second = queue.Submit(2, Root(1), 1.0);
    const auto third = queue.Submit(3, Root(1), 2.0);
    Require(first.jobId != second.jobId && second.jobId != third.jobId, "全部ちがう");
    Require(first.jobId < second.jobId && second.jobId < third.jobId, "増えていく");
}

KACHA_V2_TEST(evaluation, 結果を採用すると版が上がる)
{
    EvaluationQueue queue;
    const auto request = queue.Submit(12, Root(1), 0.0);
    const auto accepted = queue.Accept(
        EvaluationOutcome{request.jobId, request.documentRevision, false});
    Require(accepted.HasValue(), "採用できる");
    RequireEqual(std::to_string(accepted.Value()), "12", "12版");
    RequireEqual(std::to_string(queue.AcceptedRevision()), "12", "覚えている");
    Require(!queue.IsRunning(), "走り終わった");
}

KACHA_V2_TEST(evaluation, 新しい評価が来たら前のは取り消される)
{
    EvaluationQueue queue;
    const auto first = queue.Submit(1, Root(1), 0.0);
    const auto second = queue.Submit(2, Root(1), 5.0);
    Require(first.cancel.IsCancelled(), "前のは取り消された");
    Require(!second.cancel.IsCancelled(), "新しいのは生きている");
    RequireEqual(std::to_string(queue.CancelledCount()), "1", "1件");
}

KACHA_V2_TEST(evaluation, 古い評価の結果は採用しない)
{
    // AT-PER-002 そのもの。長い近似の途中で文書が変わる。
    EvaluationQueue queue;
    const auto slow = queue.Submit(1, Root(1), 0.0);
    const auto fresh = queue.Submit(2, Root(1), 50.0);
    // 遅れて、古い方が返ってくる。
    const auto stale = queue.Accept(
        EvaluationOutcome{slow.jobId, slow.documentRevision, false});
    Require(!stale.HasValue(), "採用しない");
    RequireEqual(FirstCode(stale.Diagnostics()), "PER-002", "古い結果");
    RequireEqual(std::to_string(queue.AcceptedRevision()), "0", "版は上がらない");
    // 新しい方は採用する。
    const auto good = queue.Accept(
        EvaluationOutcome{fresh.jobId, fresh.documentRevision, false});
    Require(good.HasValue(), "新しい方は採用する");
    RequireEqual(std::to_string(queue.AcceptedRevision()), "2", "2版");
}

KACHA_V2_TEST(evaluation, 番号が合っても版が違えば採用しない)
{
    // 同じ番号のまま文書だけ差し替えられた、というありえない結果も断る。
    EvaluationQueue queue;
    const auto request = queue.Submit(5, Root(1), 0.0);
    const auto wrong = queue.Accept(EvaluationOutcome{request.jobId, 4, false});
    Require(!wrong.HasValue(), "採用しない");
    RequireEqual(FirstCode(wrong.Diagnostics()), "PER-002", "古い版");
    Require(wrong.Diagnostics().front().detailsJa.find("5") != std::string::npos,
        "どの版を待っていたかを言う");
}

KACHA_V2_TEST(evaluation, 取り消した評価の結果は採用しない)
{
    EvaluationQueue queue;
    const auto request = queue.Submit(3, Root(1), 0.0);
    queue.CancelCurrent();
    Require(request.cancel.IsCancelled(), "合図が立つ");
    Require(!queue.IsRunning(), "走っていない");
    const auto late = queue.Accept(
        EvaluationOutcome{request.jobId, request.documentRevision, true});
    Require(!late.HasValue(), "採用しない");
    RequireEqual(FirstCode(late.Diagnostics()), "PER-002", "待っていない");
}

KACHA_V2_TEST(evaluation, 走っている評価が取り消しを返しても採用しない)
{
    EvaluationQueue queue;
    const auto request = queue.Submit(3, Root(1), 0.0);
    const auto cancelledOutcome = queue.Accept(
        EvaluationOutcome{request.jobId, request.documentRevision, true});
    Require(!cancelledOutcome.HasValue(), "採用しない");
    RequireEqual(FirstCode(cancelledOutcome.Diagnostics()), "PER-001", "途中でやめた");
}

KACHA_V2_TEST(evaluation, 何も待っていないのに結果が来たら捨てる)
{
    EvaluationQueue queue;
    const auto orphan = queue.Accept(EvaluationOutcome{99, 1, false});
    Require(!orphan.HasValue(), "採用しない");
    RequireEqual(FirstCode(orphan.Diagnostics()), "PER-002", "古い結果");
    RequireEqual(std::to_string(queue.DiscardedStaleCount()), "1", "1件捨てた");
}

KACHA_V2_TEST(evaluation, 何度差し替えても最後の1つだけが生きる)
{
    EvaluationQueue queue;
    std::vector<EvaluationRequest> requests;
    for (std::uint64_t revision = 1; revision <= 20; ++revision) {
        requests.push_back(queue.Submit(revision, Root(1),
            static_cast<double>(revision) * 3.0));
    }
    for (std::size_t index = 0; index + 1 < requests.size(); ++index) {
        Require(requests[index].cancel.IsCancelled(), "途中のは全部取り消し");
    }
    Require(!requests.back().cancel.IsCancelled(), "最後だけ生きている");
    // 遅れて返る順序がばらばらでも、採用するのは最後の1つだけ。
    for (std::size_t index = 0; index + 1 < requests.size(); ++index) {
        const auto late = queue.Accept(EvaluationOutcome{requests[index].jobId,
            requests[index].documentRevision, false});
        Require(!late.HasValue(), "採用しない");
    }
    const auto last = queue.Accept(EvaluationOutcome{requests.back().jobId,
        requests.back().documentRevision, false});
    Require(last.HasValue(), "最後は採用する");
    RequireEqual(std::to_string(queue.AcceptedRevision()), "20", "20版");
    RequireEqual(std::to_string(queue.DiscardedStaleCount()), "19", "19件捨てた");
}

KACHA_V2_TEST(evaluation, 100msを超えたら取消を出す)
{
    EvaluationQueue queue;
    (void)queue.Submit(1, Root(1), 1000.0);
    Require(!queue.ShouldOfferCancel(1050.0), "50msでは出さない");
    Require(!queue.ShouldOfferCancel(1100.0), "ちょうど100msでは出さない");
    Require(queue.ShouldOfferCancel(1101.0), "100msを超えたら出す");
    RequireNear(queue.ElapsedMs(1101.0), 101.0, 1e-9, "101ms");
}

KACHA_V2_TEST(evaluation, 走っていなければ取消は出さない)
{
    EvaluationQueue queue;
    Require(!queue.ShouldOfferCancel(1.0e9), "出さない");
    RequireNear(queue.ElapsedMs(1.0e9), 0.0, 1e-12, "経過0");
}

KACHA_V2_TEST(evaluation, 心拍が250ms以内なら応答している)
{
    EvaluationQueue queue;
    (void)queue.Submit(1, Root(1), 0.0);
    Require(queue.IsResponsive(200.0), "200msなら応答している");
    Require(queue.IsResponsive(250.0), "ちょうど250msも応答している");
    Require(!queue.IsResponsive(251.0), "250msを超えたら止まって見える");
    queue.Heartbeat(240.0);
    Require(queue.IsResponsive(400.0), "心拍を打てば戻る");
    Require(!queue.IsResponsive(500.0), "また止まれば分かる");
}

KACHA_V2_TEST(evaluation, 長い評価でも心拍を刻めばずっと応答している)
{
    EvaluationQueue queue;
    (void)queue.Submit(1, Root(1), 0.0);
    // 5秒かかる評価。200ms ごとに心拍を打つ。
    for (double now = 200.0; now <= 5000.0; now += 200.0) {
        queue.Heartbeat(now);
        Require(queue.IsResponsive(now), "止まって見えない");
        Require(queue.ShouldOfferCancel(now), "取消は出したまま");
    }
}

KACHA_V2_TEST(evaluation, 走っていなければいつでも応答している)
{
    EvaluationQueue queue;
    Require(queue.IsResponsive(1.0e9), "応答している");
    (void)queue.Submit(1, Root(1), 0.0);
    (void)queue.Accept(EvaluationOutcome{queue.CurrentJobId(), 1, false});
    Require(queue.IsResponsive(1.0e9), "終わったあとも応答している");
}

KACHA_V2_TEST(evaluation, 取消の合図は別のスレッドから見える)
{
    // 合図は共有する。渡した先で読めなければ意味がない。
    EvaluationQueue queue;
    const auto request = queue.Submit(1, Root(1), 0.0);
    bool seen = false;
    std::thread worker([&] {
        for (int round = 0; round < 100000 && !seen; ++round) {
            if (request.cancel.IsCancelled()) {
                seen = true;
            }
        }
    });
    queue.CancelCurrent();
    worker.join();
    Require(request.cancel.IsCancelled(), "合図が立っている");
}

KACHA_V2_TEST(evaluation, 心拍は戻らない)
{
    // 古い時刻の心拍で、応答していない状態を作り出せてはいけない。
    EvaluationQueue queue;
    (void)queue.Submit(1, Root(1), 0.0);
    queue.Heartbeat(400.0);
    queue.Heartbeat(10.0);
    Require(queue.IsResponsive(600.0), "新しい方が残る");
}

KACHA_V2_TEST_MAIN("evaluation_queue_tests")
