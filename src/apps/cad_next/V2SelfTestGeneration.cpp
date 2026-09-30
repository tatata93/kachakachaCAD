#include <QString>
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2FabricationDock.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/io/AtomicFile.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include "kachakacha/app/Selection.h"
#include <QPushButton>
#include <QLabel>
#include <QTemporaryDir>
#include <set>
#include <algorithm>
#include <cmath>
namespace kachakacha::v2::selftest {
bool MakeCurvedGuideSurface(V2MainWindow& window);
bool MakeGptHeadFixture(V2MainWindow& window);
namespace {
std::vector<domain::Entity> Generated(V2MainWindow& window)
{
    std::vector<domain::Entity> out;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities) {
        if(entity.generatedFrom) { out.push_back(entity); }
    }
    return out;
}
bool ClosedEndpoints(V2MainWindow& window,const std::vector<domain::Entity>& entities)
{
    std::vector<geometry::Vector3> endpoints;
    for(const auto& entity:entities) {
        if(entity.kind!=domain::EntityKind::Wire || entity.visibility==domain::Visibility::Hidden) { continue; }
        const auto* feature=window.Session().GetDocument().FindFeature(entity.createdBy);
        const auto* wire=feature ? std::get_if<domain::CreateWireDefinition>(&feature->definition) : nullptr;
        if(!wire || wire->segments.empty()) { return false; }
        endpoints.push_back(wire->segments.front().StartPoint()); endpoints.push_back(wire->segments.back().EndPoint());
    }
    for(std::size_t i=0;i<endpoints.size();++i) {
        bool found=false;
        for(std::size_t j=0;j<endpoints.size();++j) { if(i!=j && geometry::Distance(endpoints[i],endpoints[j])<1e-6) { found=true; } }
        if(!found) { return false; }
    }
    return !endpoints.empty();
}
bool Prepare(V2MainWindow& window,int automatic)
{
    if(!MakeCurvedGuideSurface(window)) { return false; }
    auto* mode=window.findChild<QComboBox*>(QStringLiteral("fabricationAutomaticGeneration"));
    if(!mode) { return false; } mode->setCurrentIndex(automatic);
    window.FabricationDock().SetEqualPartCount(3);
    window.FabricationDock().SetSplitAxisIndex(4);
    window.RunCommand("fabrication.create"); window.RunCommand("fabrication.create");
    return Explain("approximation created",window.FabricationModelCount()==1);
}
}
bool GenerationSelectedSurface(V2MainWindow& window)
{
    if(!Prepare(window,1)) { return false; }
    auto generated=Generated(window);
    if(!Explain("automatic wires include four edges per panel",generated.size()==12 && ClosedEndpoints(window,generated))) { return false; }
    window.RunCommand("edit.undo");
    if(!Explain("automatic generation has one undo",Generated(window).empty() && window.FabricationModelCount()==1)) { return false; }
    auto& dock=window.FabricationDock(); dock.SetPartNumbersText(QStringLiteral("2"));
    dock.TypeAssemblyPercent(37); dock.PressApplyAssembly();
    auto* output=window.findChild<QComboBox*>(QStringLiteral("fabricationGenerationOutput"));
    if(!output) { return false; } output->setCurrentIndex(3);
    window.RunCommand("fabrication.freeze_state"); generated=Generated(window); Note(window.StatusText().toStdString().c_str());
    if(!Explain("only selected panel produces four wires and one face",generated.size()==5 && ClosedEndpoints(window,generated))) { return false; }
    int surfaces=0;
    for(const auto& entity:generated) {
        if(entity.kind==domain::EntityKind::GuideSurface) { ++surfaces; }
        if(!entity.displayName.empty() && entity.displayName.find("部材2")==std::string::npos) { return Explain("selected panel numbering",false); }
    }
    if(!Explain("thin approximation face",surfaces==1 && CountOfKind(window,domain::EntityKind::Part)==0)) { return false; }
    const auto revision=window.Session().GetDocument().Revision();
    dock.SetPartNumbersText(QStringLiteral("999")); window.RunCommand("fabrication.freeze_state");
    if(!Explain("bad part number does not generate",window.Session().GetDocument().Revision()==revision)) { return false; }
    return Explain("generated thin surface survives reopen",window.SaveAndReopen(QStringLiteral("generation-selected.kcd2"))
        && Generated(window).size()==5 && CountOfKind(window,domain::EntityKind::GuideSurface)==2);
}
bool GenerationSeparateDocument(V2MainWindow& window)
{
    if(!Prepare(window,0)) { return false; }
    auto& dock=window.FabricationDock(); dock.SetPartNumbersText(QStringLiteral("2"));
    dock.TypeAssemblyPercent(41); dock.PressApplyAssembly(); dock.SetGenerationDestination(1);
    auto* output=window.findChild<QComboBox*>(QStringLiteral("fabricationGenerationOutput"));
    if(!output) { return false; } output->setCurrentIndex(3);
    const auto revision=window.Session().GetDocument().Revision(); const auto pathBefore=window.DocumentPath();
    window.SetPathChooser([](bool){return QString();}); window.RunCommand("fabrication.freeze_state");
    if(!Explain("cancel export leaves source unchanged",window.Session().GetDocument().Revision()==revision && Generated(window).empty())) { return false; }
    QTemporaryDir folder; const auto path=folder.path()+QStringLiteral("/generated.kcd2");
    window.SetPathChooser([path](bool){return path;}); window.RunCommand("fabrication.freeze_state"); Note(window.StatusText().toStdString().c_str());
    if(!Explain("export does not modify source or save path",window.Session().GetDocument().Revision()==revision
        && window.DocumentPath()==pathBefore && Generated(window).empty())) { return false; }
    const auto bytes=io::ReadWholeFile(path.toStdString()); if(!bytes.HasValue()) { return Explain("export file exists",false); }
    const auto file=io::LoadDocument(bytes.Value());
    if(!Explain("independent document contains selected panel only",file.HasValue() && file.Value().snapshot.entities.size()==5)) { return false; }
    for(const auto& entity:file.Value().snapshot.entities) {
        if(entity.generatedFrom || entity.kind==domain::EntityKind::FabricationModel) { return false; }
    }
    if(!window.OpenDocumentFile(path)) { return false; }
    return Explain("exported approximation face rebuilds",CountOfKind(window,domain::EntityKind::GuideSurface)==1
        && CountOfKind(window,domain::EntityKind::Wire)==4 && window.FabricationModelCount()==0);
}
bool GenerationBendStates(V2MainWindow& window)
{
    if(!MakeGptHeadFixture(window)) { return false; }
    window.Viewport().SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::GuideSurface));
    window.FabricationDock().SetEqualPartCount(3);
    window.RunCommand("fabrication.create"); window.RunCommand("fabrication.create");
    if(!Explain("head approximation created",window.FabricationModelCount()==1)) { return false; }
    std::vector<std::vector<geometry::Vector3>> states;
    std::vector<double> lengths;
    for(double percent:{0.0,37.0,100.0}) {
        auto& dock=window.FabricationDock(); dock.SetPartNumbersText(QStringLiteral("2"));
        dock.TypeAssemblyPercent(percent); dock.PressApplyAssembly(); Note(window.StatusText().toStdString().c_str()); window.RunCommand("fabrication.freeze_state");
        const auto generated=Generated(window);
        if(!Explain("every bend state has a closed four-edge boundary",generated.size()==4 && ClosedEndpoints(window,generated))) { return false; }
        std::vector<geometry::Vector3> points; double length=0;
        for(const auto& entity:generated) {
            const auto* feature=window.Session().GetDocument().FindFeature(entity.createdBy);
            const auto* wire=feature ? std::get_if<domain::CreateWireDefinition>(&feature->definition) : nullptr;
            if(!wire) { return false; }
            for(const auto& segment:wire->segments) { points.push_back(segment.StartPoint()); length+=segment.TotalLength(1e-6); }
        }
        states.push_back(std::move(points)); lengths.push_back(length); window.RunCommand("edit.undo");
    }
    for(std::size_t i=1;i<states.size();++i) {
        if(states[i].size()!=states[0].size()) { return false; }
        double difference=0;
        for(std::size_t j=0;j<states[i].size();++j) { difference=std::max(difference,geometry::Distance(states[i][j],states[i-1][j])); }
        if(!Explain("0/37/100 percent produce different positions",difference>0.01)) { return false; }
        if(!Explain("bending preserves boundary length",std::abs(lengths[i]-lengths[0])<0.05)) { return false; }
    }
    return true;
}
bool GenerationGptAutomatic(V2MainWindow& window)
{
    if(!MakeGptHeadFixture(window)) { return false; }
    window.Viewport().SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::GuideSurface));
    auto* mode=window.findChild<QComboBox*>(QStringLiteral("fabricationAutomaticGeneration"));
    if(!mode) { return false; } mode->setCurrentIndex(2);
    window.RunCommand("fabrication.gpt_create");
    auto* width=window.findChild<QDoubleSpinBox*>(QStringLiteral("gptFabricationWidth"));
    if(!width) { return false; } width->setValue(.2);
    for(const auto* name:{"gptFabricationPreview","gptFabricationConfirm"}) {
        auto* button=window.findChild<QPushButton*>(QString::fromLatin1(name));
        if(!button || !button->isEnabled()) {
            auto* status=window.findChild<QLabel*>(QStringLiteral("gptFabricationStatus"));
            if(status) { Note(status->text().toStdString().c_str()); }
            return Explain("GPT generation button enabled",false);
        } button->click();
    }
    Note(window.StatusText().toStdString().c_str());
    auto generated=Generated(window); int wires=0,surfaces=0;
    for(const auto& entity:generated) {
        if(entity.kind==domain::EntityKind::Wire && entity.visibility!=domain::Visibility::Hidden) { ++wires; }
        if(entity.kind==domain::EntityKind::GuideSurface) { ++surfaces; }
    }
    if(!Explain("GPT automatic wires and approximation surfaces",wires>0 && wires==surfaces && ClosedEndpoints(window,generated))) { return false; }
    return window.SaveAndReopen(QStringLiteral("gpt-generated.kcd2")) && Generated(window).size()==generated.size();
}
}
