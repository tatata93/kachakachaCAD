#include "kachakacha/base/Diagnostic.h"

namespace kachakacha::v2::base {

namespace {

[[nodiscard]] Diagnostic Make(Severity severity, std::string code, std::string summaryJa,
    std::string detailsJa)
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.summaryJa = std::move(summaryJa);
    diagnostic.detailsJa = std::move(detailsJa);
    return diagnostic;
}

} // namespace

Diagnostic MakeError(std::string code, std::string summaryJa, std::string detailsJa)
{
    return Make(Severity::Error, std::move(code), std::move(summaryJa), std::move(detailsJa));
}

Diagnostic MakeWarning(std::string code, std::string summaryJa, std::string detailsJa)
{
    return Make(Severity::Warning, std::move(code), std::move(summaryJa),
        std::move(detailsJa));
}

Diagnostic MakeInformation(std::string code, std::string summaryJa, std::string detailsJa)
{
    return Make(Severity::Information, std::move(code), std::move(summaryJa),
        std::move(detailsJa));
}

} // namespace kachakacha::v2::base
