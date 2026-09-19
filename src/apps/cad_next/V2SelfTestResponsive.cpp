//! 撮影の5場面がすべて作れるか(HP-RS、指示書 I-02)。
//!
//! I-02 は 1280x720/1920x1080/2560x1440 × 100/125/150% で5つの場面を撮る。
//! ここでは画面を出さずに、その5つの場面が本当に作れることだけを確かめる。
//! 解像度・DPI の掛け合わせそのものは PC のバッチ([5] snapshots)が撮る。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Ribbon.h"

#include <QApplication>
#include <QString>

#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

//! HP-RS-01。5つの場面がどれも作れ、1280x720 でも帯のボタンが幅に収まる。
[[nodiscard]] bool CaseAllResponsiveShotsCanBeBuilt(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("ui-ribbon-drawing が作れる",
            window.ApplyManualState(QStringLiteral("ui-ribbon-drawing")))) {
        return false;
    }
    window.resize(1280, 720);
    QApplication::processEvents();
    if (!Explain("1280x720 でも帯のボタンが幅に収まる", window.Ribbon().ToolsFitInWidth())) {
        return false;
    }
    const char* const names[4] = {"ui-ribbon-part-thicken", "ui-fab-generate",
        "ui-explorer-groups", "ui-measure-overlay"};
    for (const char* name : names) {
        window.RunCommand("file.new");
        const bool built = window.ApplyManualState(QString::fromUtf8(name));
        // 作れなかったときは、そのときの案内も添える。場面づくりは何段もあるので、
        // 「作れない」だけでは PC のログからどこで止まったか読めない。
        if (!Explain((std::string(name) + " が作れる(" + window.StatusText().toStdString()
                         + ")").c_str(),
                built)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<SelfTestCase> ResponsiveCases()
{
    return {
        {"HP-RS-01 5 つの撮影の場面が作れる", CaseAllResponsiveShotsCanBeBuilt},
    };
}

} // namespace kachakacha::v2::selftest
