#include "kachakacha/modeling/SubshapeKey.h"

#include <algorithm>

namespace kachakacha::v2::modeling {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kBadKey = "GEO-K001";

[[nodiscard]] std::vector<std::string> Split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t found = text.find(separator, start);
        if (found == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, found - start));
        start = found + 1;
    }
    return parts;
}

} // namespace

std::string SubshapeKey::ToString() const
{
    switch (kind) {
    case SubshapeKind::ExtrudeCapStart:
        return "extrude/cap/start";
    case SubshapeKind::ExtrudeCapEnd:
        return "extrude/cap/end";
    case SubshapeKind::ExtrudeSide:
        return "extrude/side/" + sourceSegmentId.ToString();
    case SubshapeKind::LoftSpan:
        return "loft/span/" + firstSectionSegmentId.ToString() + "/"
            + secondSectionSegmentId.ToString();
    case SubshapeKind::CagePatch:
        return "cage/patch/" + sourceSegmentId.ToString();
    case SubshapeKind::BooleanProvenance:
        return "boolean/provenance/" + sourcePartId.ToString() + "/" + nestedKey;
    }
    return {};
}

Result<SubshapeKey> ParseSubshapeKey(std::string_view text)
{
    const std::vector<std::string> parts = Split(text, '/');
    const auto fail = [&](const std::string& why) {
        return Result<SubshapeKey>::Failure(MakeError(kBadKey,
            "部品の面を指す記号を読めません。", std::string(text) + " : " + why));
    };
    if (parts.size() < 3) {
        return fail("区切りが足りません。");
    }
    SubshapeKey key;
    if (parts[0] == "extrude" && parts[1] == "cap" && parts.size() == 3) {
        if (parts[2] == "start") {
            key.kind = SubshapeKind::ExtrudeCapStart;
            return Result<SubshapeKey>::Success(key);
        }
        if (parts[2] == "end") {
            key.kind = SubshapeKind::ExtrudeCapEnd;
            return Result<SubshapeKey>::Success(key);
        }
        return fail("押し出しの端は start か end です。");
    }
    if (parts[0] == "extrude" && parts[1] == "side" && parts.size() == 3) {
        const auto id = SegmentId::Parse(parts[2]);
        if (!id.has_value()) {
            return fail("線のIDとして読めません。");
        }
        key.kind = SubshapeKind::ExtrudeSide;
        key.sourceSegmentId = *id;
        return Result<SubshapeKey>::Success(key);
    }
    if (parts[0] == "loft" && parts[1] == "span" && parts.size() == 4) {
        const auto first = SegmentId::Parse(parts[2]);
        const auto second = SegmentId::Parse(parts[3]);
        if (!first.has_value() || !second.has_value()) {
            return fail("断面の線のIDとして読めません。");
        }
        key.kind = SubshapeKind::LoftSpan;
        key.firstSectionSegmentId = *first;
        key.secondSectionSegmentId = *second;
        return Result<SubshapeKey>::Success(key);
    }
    if (parts[0] == "cage" && parts[1] == "patch" && parts.size() == 3) {
        const auto id = SegmentId::Parse(parts[2]);
        if (!id.has_value()) {
            return fail("線のIDとして読めません。");
        }
        key.kind = SubshapeKind::CagePatch;
        key.sourceSegmentId = *id;
        return Result<SubshapeKey>::Success(key);
    }
    if (parts[0] == "boolean" && parts[1] == "provenance" && parts.size() >= 4) {
        const auto id = EntityId::Parse(parts[2]);
        if (!id.has_value()) {
            return fail("元の部品のIDとして読めません。");
        }
        // 入れ子のキーは残り全部。さらに boolean を重ねても壊れない。
        std::string nested = parts[3];
        for (std::size_t at = 4; at < parts.size(); ++at) {
            nested += "/" + parts[at];
        }
        const auto inner = ParseSubshapeKey(nested);
        if (!inner.HasValue()) {
            return fail("中に入っている記号が読めません。");
        }
        key.kind = SubshapeKind::BooleanProvenance;
        key.sourcePartId = *id;
        key.nestedKey = nested;
        return Result<SubshapeKey>::Success(key);
    }
    return fail("知らない種類です。");
}

SubshapeKey MakeExtrudeCapStart()
{
    SubshapeKey key;
    key.kind = SubshapeKind::ExtrudeCapStart;
    return key;
}

SubshapeKey MakeExtrudeCapEnd()
{
    SubshapeKey key;
    key.kind = SubshapeKind::ExtrudeCapEnd;
    return key;
}

SubshapeKey MakeExtrudeSide(SegmentId sourceSegmentId)
{
    SubshapeKey key;
    key.kind = SubshapeKind::ExtrudeSide;
    key.sourceSegmentId = sourceSegmentId;
    return key;
}

SubshapeKey MakeLoftSpan(SegmentId first, SegmentId second)
{
    SubshapeKey key;
    key.kind = SubshapeKind::LoftSpan;
    key.firstSectionSegmentId = first;
    key.secondSectionSegmentId = second;
    return key;
}

SubshapeKey MakeCagePatch(SegmentId representativeSegmentId)
{
    SubshapeKey key;
    key.kind = SubshapeKind::CagePatch;
    key.sourceSegmentId = representativeSegmentId;
    return key;
}

SubshapeKey MakeBooleanProvenance(EntityId sourcePartId, const std::string& nestedKey)
{
    SubshapeKey key;
    key.kind = SubshapeKind::BooleanProvenance;
    key.sourcePartId = sourcePartId;
    key.nestedKey = nestedKey;
    return key;
}

} // namespace kachakacha::v2::modeling
