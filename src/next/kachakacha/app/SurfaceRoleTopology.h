#pragma once

//! おまかせ(app/SurfaceRoleAssist)の中身: 線を調べる道具(つながり・交わり・閉じた輪・
//! 二部に分ける)と、候補を幾何の検査に通す道具。外へ見せる API ではない
//! (SurfaceRoleAssist.cpp だけが使う)。

#include "kachakacha/app/SurfaceRoleAssist.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::app::detail {

using geometry::Vector3;
using modeling::GuideSurfaceMethod;

//! 線 1 本を調べた結果。閉じた線も、点列は最後に最初の点を持つ(閉じる辺を含める)。
struct Probe {
    base::EntityId id;
    std::size_t order = 0;
    const RoleWire* wire = nullptr;
    std::vector<Vector3> points;
    Vector3 start{};
    Vector3 end{};
    Vector3 centroid{};
    Vector3 low{};
    Vector3 high{};
    double lengthMm = 0.0;
    double sizeMm = 0.0;
    double chordMm = 0.0;
    bool closed = false;
    bool valid = false;
};

//! 2 本の線の交わり(触れる・交わる)。
struct Contact {
    std::size_t a = 0;
    std::size_t b = 0;
    //! それぞれの線の上の位置(正規化弧長)。
    double onA = 0.0;
    double onB = 0.0;
    Vector3 at{};
    //! 交わる所の数(2 以上は 2 重の交わり)。
    int count = 1;
    bool atEndOfA = false;
    bool atEndOfB = false;
};

struct Topology {
    std::vector<Probe> probes;
    std::vector<Contact> contacts;
    double joinMm = 0.01;

    [[nodiscard]] const Contact* Between(std::size_t a, std::size_t b) const
    {
        for (const Contact& contact : contacts) {
            if ((contact.a == a && contact.b == b) || (contact.a == b && contact.b == a)) {
                return &contact;
            }
        }
        return nullptr;
    }
    //! a から見た b の上の位置(正規化弧長)。
    [[nodiscard]] double PositionOn(std::size_t on, std::size_t other) const
    {
        const Contact* contact = Between(on, other);
        if (contact == nullptr) {
            return 0.0;
        }
        return contact->a == on ? contact->onA : contact->onB;
    }
    [[nodiscard]] std::vector<std::size_t> Neighbors(std::size_t index) const
    {
        std::vector<std::size_t> out;
        for (const Contact& contact : contacts) {
            if (contact.a == index) {
                out.push_back(contact.b);
            } else if (contact.b == index) {
                out.push_back(contact.a);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }
};

struct Loop {
    std::vector<std::size_t> members;
    bool planar = false;
    geometry::PlaneFit plane;
    double sizeMm = 0.0;
};

struct Ladder {
    bool found = false;
    std::vector<std::size_t> sections;
    std::vector<std::size_t> rails;
    //! すべての断面がすべてのガイドと 1 か所で交わる。
    bool complete = false;
    std::vector<std::string> problems;
};

//! 候補 1 つぶんの役割(線ごと)と、作り方。
struct Draft {
    GuideSurfaceMethod method = GuideSurfaceMethod::LoftSections;
    std::vector<WireRoleChoice> roles;
    std::vector<std::string> reasons;
    std::string reasonJa;
    //! 幾何の検査に通さずに成り立たないと分かっているときの理由(平面に通る線など)。
    std::string refusalJa;
};

//! 線を全部調べる(点列にし、2 本ずつ交わりを探す)。
[[nodiscard]] Topology Examine(const std::vector<RoleWire>& wires,
    const geometry::GeometryTolerance& tolerance);

//! 端どうしでつながって閉じた輪(開いた線 2 本以上)と、1 本で閉じた線。
//! eligible が偽の線(人が断面・ガイド・中心線・通る線に決めた線)は輪に入れない。
[[nodiscard]] std::vector<Loop> FindLoops(const Topology& topology,
    const std::vector<bool>& eligible);

//! 点が、平らな輪の内側にあるか(輪の平面へ落として数える)。
[[nodiscard]] bool InsideLoop(const Topology& topology, const Loop& loop, const Vector3& point);

//! members の交わりを二部(互いに交わらない 2 組)に分ける。どちらが断面かは、
//! 人の決めた役割 → 閉じた線 → 本数 → 長さ → 選んだ順で決める。分けられなければ found = false。
[[nodiscard]] Ladder SplitIntoLadder(const Topology& topology,
    const std::vector<std::size_t>& members, const std::vector<WireRoleChoice>& fixed);

//! 候補を幾何の検査に通す。平面と境界面は、輪の検査(つながり・同じ平面)で済ませる。
[[nodiscard]] SurfaceMethodCandidate Check(const Topology& topology, const Draft& draft,
    const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::app::detail
