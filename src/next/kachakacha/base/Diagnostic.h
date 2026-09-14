#pragma once

//! 診断。例外文の文字列一致ではなく、固定コードと日本語文の組で扱う。
//! 現行版は英語例外文を584件の対訳表で日本語化していたが、
//! コード化されていないため機械で追えなかった。V2はコードを正本にする。

#include "kachakacha/base/Ids.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kachakacha::v2::base {

enum class Severity {
    Information,
    Warning,
    Error,
};

[[nodiscard]] constexpr std::string_view SeverityName(Severity severity) noexcept
{
    switch (severity) {
    case Severity::Information: return "Information";
    case Severity::Warning:     return "Warning";
    case Severity::Error:       return "Error";
    }
    return "Unknown";
}

//! 直し方の提示。UIはこれをボタンにする。
struct RecoveryAction {
    std::string code;      //!< 機械が見る安定ID
    std::string labelJa;   //!< 画面に出す日本語
};

struct Diagnostic {
    std::string code;                       //!< 例 "GEO-W001"。空にしてはならない
    Severity severity = Severity::Error;
    std::string summaryJa;                  //!< 何が起きたか。1文
    std::string detailsJa;                  //!< どこが、どう直せるか
    std::vector<EntityId> entityIds;
    std::vector<FeatureId> featureIds;
    std::vector<std::string> subshapeKeys;
    std::vector<RecoveryAction> recoveryActions;

    [[nodiscard]] bool IsError() const noexcept { return severity == Severity::Error; }
};

//! よく使う組み立て。code と summaryJa は必須。
[[nodiscard]] Diagnostic MakeError(std::string code, std::string summaryJa,
    std::string detailsJa = {});
[[nodiscard]] Diagnostic MakeWarning(std::string code, std::string summaryJa,
    std::string detailsJa = {});
[[nodiscard]] Diagnostic MakeInformation(std::string code, std::string summaryJa,
    std::string detailsJa = {});

//! 値か診断のどちらかを返す。Errorが1件でもあれば値を持たない。
template<class T>
class Result {
public:
    [[nodiscard]] static Result Success(T value, std::vector<Diagnostic> warnings = {})
    {
        Result result;
        result.diagnostics_ = std::move(warnings);
        // 呼び出し側の取り違えを早く見つけるため、Errorが混ざっていたら値を渡さない。
        bool hasError = false;
        for (const Diagnostic& diagnostic : result.diagnostics_) {
            if (diagnostic.IsError()) {
                hasError = true;
                break;
            }
        }
        if (!hasError) {
            result.value_.emplace(std::move(value));
        }
        return result;
    }

    //! 断る。**理由が1つも無い断り方は作らせない。**
    //!
    //! 「できないことを、できたことにしない」の裏側として、
    //! 「断るなら理由を言う」がある。理由の無い失敗は、画面が
    //! `Diagnostics().front()` を読んだ瞬間に並びの外を読む。
    //! Release では気づかず、Windows の Debug では落ちる。
    //! ここで必ず1つ入れておけば、読む側は数えなくてよくなる。
    [[nodiscard]] static Result Failure(std::vector<Diagnostic> errors)
    {
        Result result;
        result.diagnostics_ = std::move(errors);
        if (result.diagnostics_.empty()) {
            result.diagnostics_.push_back(MakeError("GEN-E000",
                "理由の付いていない失敗です。",
                "断るときは理由番号と一文を付けてください(不具合)。"));
        }
        return result;
    }

    [[nodiscard]] static Result Failure(Diagnostic error)
    {
        return Failure(std::vector<Diagnostic>{std::move(error)});
    }

    [[nodiscard]] bool HasValue() const noexcept { return value_.has_value(); }
    //! HasValue() が false のときに呼んではならない。
    [[nodiscard]] const T& Value() const { return *value_; }
    [[nodiscard]] const std::vector<Diagnostic>& Diagnostics() const noexcept
    {
        return diagnostics_;
    }
    //! 最初の理由の一文。理由が1つも無ければ、そう言う。
    //!
    //! `Diagnostics().front()` を直に書くと、理由を付け忘れた失敗で
    //! **並びの外を読む**。Release では気づかず、Windows の Debug では落ちる。
    //! 落ちるより「理由が入っていません」と出たほうが、直しようがある。
    [[nodiscard]] std::string FirstSummaryJa() const
    {
        return diagnostics_.empty() ? std::string("理由が入っていません(不具合)。")
                                    : diagnostics_.front().summaryJa;
    }
    [[nodiscard]] bool HasError() const noexcept
    {
        for (const Diagnostic& diagnostic : diagnostics_) {
            if (diagnostic.IsError()) {
                return true;
            }
        }
        return false;
    }

private:
    std::optional<T> value_;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace kachakacha::v2::base
