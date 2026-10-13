#pragma once

#include "kachakacha/app/GeometryJobDiagnostic.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
class QWidget;

namespace V2Calculation {
bool BackgroundAllowed();
bool Execute(const QWidget* parent, const char* title, const std::function<void()>& work);

// Only newly built results are discarded. Input handles are never released.
template<class T> void Discard(const T& value) {
    if constexpr (requires { value.handle.Valid(); }) {
        if (value.handle.Valid()) kachakacha::v2::kernel::ReleaseShape(value.handle);
    }
    if constexpr (requires { value.parts; }) for (const auto& part : value.parts) Discard(part);
    if constexpr (requires { value.pieces; }) for (const auto& piece : value.pieces) Discard(piece);
    if constexpr (requires { value.faces.front().Valid(); }) {
        for (const auto& face : value.faces) if (face.Valid()) kachakacha::v2::kernel::ReleaseShape(face);
    }
    if constexpr (requires { value.positive.Valid(); }) {
        if (value.positive.Valid()) kachakacha::v2::kernel::ReleaseShape(value.positive);
    }
    if constexpr (requires { value.negative.Valid(); }) {
        if (value.negative.Valid()) kachakacha::v2::kernel::ReleaseShape(value.negative);
    }
}

// Arguments are copied on the GUI thread, before any native calculation starts.
// The worker cannot read widgets or a changing document through these values.
template<class Function, class... Args>
auto Call(const QWidget* parent, const char* title, Function function, Args&&... args) {
    auto values = std::make_tuple(std::forward<Args>(args)...);
    using Out = decltype(std::apply(function, values));
    if (!BackgroundAllowed()) return std::apply(function, values);
    std::optional<Out> result;
    try {
        const bool cancelled = Execute(parent, title, [&] { result.emplace(std::apply(function, values)); });
        if (cancelled) {
            if (result && result->HasValue()) Discard(result->Value());
            return Out::Failure(kachakacha::v2::base::MakeError(
                kachakacha::v2::app::kGeometryJobCancelled, "計算を取り消しました。文書は変更していません。", {}));
        }
        if (result) return std::move(*result);
    } catch (const std::exception& error) {
        return Out::Failure(kachakacha::v2::base::MakeError(
            kachakacha::v2::app::kGeometryJobFailed, "計算を完了できませんでした。", error.what()));
    } catch (...) {}
    return Out::Failure(kachakacha::v2::base::MakeError(
        kachakacha::v2::app::kGeometryJobFailed, "計算を完了できませんでした。", {}));
}
}
