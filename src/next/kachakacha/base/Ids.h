#pragma once

//! 種類ごとに別の型のID。文字列とも、他の種類のIDとも暗黙変換できない。
//! 「表示名で参照する」という現行版の弱点を、型で塞ぐのが目的(DOC-002 / 4.1)。

#include "kachakacha/base/Uuid.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace kachakacha::v2::base {

//! 種類タグ。IDの型を分けるためだけに使う。
enum class IdKind : std::uint8_t {
    Document,
    Entity,
    Feature,
    Group,
    Segment,
    Panel,
    Fold,
    Cut,
    Asset,
};

[[nodiscard]] constexpr std::string_view IdKindName(IdKind kind) noexcept
{
    switch (kind) {
    case IdKind::Document: return "Document";
    case IdKind::Entity:   return "Entity";
    case IdKind::Feature:  return "Feature";
    case IdKind::Group:    return "Group";
    case IdKind::Segment:  return "Segment";
    case IdKind::Panel:    return "Panel";
    case IdKind::Fold:     return "Fold";
    case IdKind::Cut:      return "Cut";
    case IdKind::Asset:    return "Asset";
    }
    return "Unknown";
}

template<IdKind Kind>
class TypedId {
public:
    static constexpr IdKind kKind = Kind;

    constexpr TypedId() noexcept = default;
    explicit constexpr TypedId(Uuid value) noexcept : value_(value) {}

    [[nodiscard]] static std::optional<TypedId> Parse(std::string_view text)
    {
        const std::optional<Uuid> parsed = Uuid::Parse(text);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        return TypedId(*parsed);
    }

    [[nodiscard]] const Uuid& Value() const noexcept { return value_; }
    [[nodiscard]] std::string ToString() const { return value_.ToString(); }
    [[nodiscard]] bool IsNil() const noexcept { return value_.IsNil(); }

    //! 同名が並ぶときにUIへ添える末尾4桁(4.2)。
    [[nodiscard]] std::string ShortSuffix() const
    {
        const std::string text = value_.ToString();
        return text.substr(text.size() - 4);
    }

    friend bool operator==(const TypedId& l, const TypedId& r) noexcept
    {
        return l.value_ == r.value_;
    }
    friend bool operator!=(const TypedId& l, const TypedId& r) noexcept { return !(l == r); }
    friend bool operator<(const TypedId& l, const TypedId& r) noexcept
    {
        return l.value_ < r.value_;
    }
    friend bool operator>(const TypedId& l, const TypedId& r) noexcept { return r < l; }
    friend bool operator<=(const TypedId& l, const TypedId& r) noexcept { return !(r < l); }
    friend bool operator>=(const TypedId& l, const TypedId& r) noexcept { return !(l < r); }

private:
    Uuid value_{};
};

using DocumentId = TypedId<IdKind::Document>;
using EntityId = TypedId<IdKind::Entity>;
using FeatureId = TypedId<IdKind::Feature>;
using GroupId = TypedId<IdKind::Group>;
using SegmentId = TypedId<IdKind::Segment>;
using PanelId = TypedId<IdKind::Panel>;
using FoldId = TypedId<IdKind::Fold>;
using CutId = TypedId<IdKind::Cut>;
using AssetId = TypedId<IdKind::Asset>;

//! IDを配る側。試験は固定列を注入して、結果を毎回同じにする。
class IdGenerator {
public:
    virtual ~IdGenerator() = default;
    [[nodiscard]] virtual Uuid Next() = 0;

    template<IdKind Kind>
    [[nodiscard]] TypedId<Kind> NextTyped()
    {
        return TypedId<Kind>(Next());
    }
};

//! 本番用。UUID v4(乱数)。
class RandomIdGenerator final : public IdGenerator {
public:
    explicit RandomIdGenerator(std::uint64_t seed = 0);
    [[nodiscard]] Uuid Next() override;

private:
    std::uint64_t state0_ = 0;
    std::uint64_t state1_ = 0;
};

//! 試験用。seedから決定的に作る。同じseedなら毎回同じ列。
class DeterministicIdGenerator final : public IdGenerator {
public:
    explicit DeterministicIdGenerator(std::uint64_t seed = 1) : counter_(seed) {}
    [[nodiscard]] Uuid Next() override;

private:
    std::uint64_t counter_ = 1;
};

} // namespace kachakacha::v2::base

template<kachakacha::v2::base::IdKind Kind>
struct std::hash<kachakacha::v2::base::TypedId<Kind>> {
    [[nodiscard]] std::size_t operator()(
        const kachakacha::v2::base::TypedId<Kind>& value) const noexcept
    {
        return std::hash<kachakacha::v2::base::Uuid>{}(value.Value());
    }
};
