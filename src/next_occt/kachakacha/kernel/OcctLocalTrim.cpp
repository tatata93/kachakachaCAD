#include "kachakacha/kernel/OcctContact.h"
#include <algorithm>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#endif

namespace kachakacha::v2::kernel {
base::Result<ContactResult> BuildLocalTrim(modeling::KernelShapeHandle first,
    modeling::KernelShapeHandle second, const std::vector<int>& removals, double tolerance)
{
    using Out = base::Result<ContactResult>;
#ifdef KACHACAD_V2_WITH_OCCT
    const auto overlap = BuildContact(first, second, false, false, true, tolerance);
    if (!overlap.HasValue()) return Out::Failure(overlap.Diagnostics());
    if (removals.size() != overlap.Value().pieces.size()
        || std::any_of(removals.begin(), removals.end(), [](int mask) { return mask < 0 || mask > 3; }))
        return Out::Failure(base::MakeError("KER-CT05", "重なり領域が変わりました。領域を選び直してください。", {}));
    if (std::all_of(removals.begin(), removals.end(), [](int mask) { return mask == 0; }))
        return Out::Failure(base::MakeError("KER-CT05", "削る領域を選んでください。", {}));
    try {
        ContactResult result;
        const modeling::KernelShapeHandle sources[]{first, second};
        for (int side = 0; side < 2; ++side) {
            const int flag = side == 0 ? 1 : 2;
            if (std::none_of(removals.begin(), removals.end(), [flag](int mask) { return (mask & flag) != 0; })) continue;
            TopoDS_Shape current;
            if (!LookupShape(sources[side], current)) return Out::Failure(base::MakeError("KER-CT00", "元の部品がありません。", {}));
            for (std::size_t i = 0; i < removals.size(); ++i) {
                if ((removals[i] & flag) == 0) continue;
                TopoDS_Shape region;
                if (!LookupShape(overlap.Value().pieces[i].handle, region)) return Out::Failure(base::MakeError("KER-CT00", "領域がありません。", {}));
                BRepAlgoAPI_Cut cut(current, region);
                if (!cut.IsDone()) return Out::Failure(base::MakeError("KER-CT03", "指定領域を削れません。", {}));
                current = cut.Shape();
            }
            const auto pieces = ContactSolidPieces(StoreShape(current), tolerance);
            if (!pieces.HasValue()) return Out::Failure(pieces.Diagnostics());
            // An empty body needs an explicit deletion command, not an invisible result.
            if (pieces.Value().empty()) return Out::Failure(base::MakeError("KER-CT04", "指定した削除で部品がすべて無くなります。", "領域の指定を変更してください。"));
            for (auto piece : pieces.Value()) { piece.sourceSide = side; result.pieces.push_back(piece); }
        }
        return Out::Success(std::move(result));
    } catch (const Standard_Failure& failure) {
        return Out::Failure(base::MakeError("KER-CT03", "指定領域を削れません。", failure.GetMessageString()));
    } catch (...) { return Out::Failure(base::MakeError("KER-CT03", "指定領域を削れません。", {})); }
#else
    (void)first; (void)second; (void)removals; (void)tolerance;
    return Out::Failure(base::MakeError("KER-CT00", "幾何カーネルが必要です。", {}));
#endif
}
}
