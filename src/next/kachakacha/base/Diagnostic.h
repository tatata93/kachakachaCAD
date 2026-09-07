#pragma once

//! 診断。例外文の文字列一致ではなく、固定コードと日本語文の組で扱う。
//! 現行版は英語例外文を584件の対訳表で日本語化していたが、
//! コード化されていないため機械で追えなかった。V2はコードを正本にする。

#include "kachakacha/base/Ids.h"

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
        result.value_ = std::move(value);
        result.hasValue_ = true;
        result.diagnostics_ = std::move(warnings);
        // 呼び出し側の取り違えを早く見つけるため、Errorが混ざっていたら値を落とす。
        for (const Diagnostic& diagnostic : result.diagnostics_) {
            if (diagnostic.IsError()) {
                result.hasValue_ = false;
                result.value_ = T{};
                break;
            }
        }
        return result;
    }

    [[nodiscard]] static Result Failure(std::vector<Diagnostic> errors)
    {
        Result result;
        result.hasValue_ = false;
        result.diagnostics_ = std::move(errors);
        return result;
    }

    [[nodiscard]] static Result Failure(Diagnostic error)
    {
        return Failure(std::vector<Diagnostic>{std::move(error)});
    }

    [[nodiscard]] bool HasValue() const noexcept { return hasValue_; }
    [[nodiscard]] const T& Value() const { return value_; }
    [[nodiscard]] const std::vector<Diagnostic>& Diagnostics() const noexcept
    {
        return diagnostics_;
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
    T value_{};
    bool hasValue_ = false;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace kachakacha::v2::base
