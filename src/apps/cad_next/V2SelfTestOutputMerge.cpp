#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2OutputTool.h"
#include "kachakacha/app/DocumentIds.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTemporaryDir>
#include <algorithm>

namespace kachakacha::v2::selftest {
namespace {
V2OutputTool* CurrentOutput(V2MainWindow& w) {
    for(auto* dialog:w.findChildren<QDialog*>())if(dialog->objectName()=="selectionOutput"&&dialog->isVisible())
        if(auto* output=dynamic_cast<V2OutputTool*>(dialog))return output;
    return nullptr;
}
bool MakeSource(V2MainWindow& w) {
    w.RunCommand("file.new");if(!DrawRectangleByHand(w))return false;
    const auto& entities=w.Session().GetDocument().Snapshot().entities;
    for(const auto& e:entities)if(e.kind==domain::EntityKind::Wire){if(!w.SelectTreeRowForEntity(e.id))return false;break;}
    w.RunCommand("part.extrude");w.RunCommand("part.extrude");
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::Part)return w.SelectTreeRowForEntity(e.id);
    return false;
}
bool SaveSource(V2MainWindow& w,const QString& path) {
    w.SetPathChooser([path](bool save){return save?path:QString();});w.RunCommand("file.save_as");
    return QFile::exists(path);
}
bool MakeTarget(const QString& path,io::DocumentFile& file,std::string& bytes) {
    V2MainWindow target;target.resize(1200,800);target.show();QApplication::processEvents();
    if(!MakeSource(target))return false;
    target.RunCommand("group.create");
    file.snapshot=target.Session().GetDocument().Snapshot();
    // Non-geometric archive fixture: exercise preservation of metadata, references and asset bytes.
    file.metadata.author="merge fixture";file.metadata.description="preserve existing history";
    file.uiState=io::JsonValue::Object({{"mergeFixture",io::JsonValue::String("unchanged")}});
    file.sideEntries.push_back({"assets/preserved.bin",std::string("image\0bytes",11)});
    document::ReferenceDimension dimension;dimension.id=*base::DimensionId::Parse("00000000-0000-4000-8000-0000000f0000");
    dimension.label="retained reference";dimension.kind="two_points";dimension.unit="mm";dimension.recordedValue=1;
    dimension.targets={file.snapshot.entities.back().id};file.snapshot.referenceDimensions.push_back(dimension);
    const auto archive=io::SaveDocument(file);if(!archive.HasValue())return false;bytes=archive.Value();
    return io::WriteFileAtomically(path.toStdString(),bytes).HasValue();
}
V2OutputTool* OpenMerge(V2MainWindow& w,const QString& target) {
    w.RunCommand("output.copy");auto* output=CurrentOutput(w);if(!output)return nullptr;
    auto* format=output->findChild<QComboBox*>("outputFormat");if(!format||format->count()!=5)return nullptr;
    format->setCurrentIndex(4);auto* choose=output->findChild<QPushButton*>("outputMergeChoose");
    if(!choose||!choose->isVisible())return nullptr;
    w.SetPathChooser([target](bool){return target;});choose->click();return output;
}
bool SameFile(const QString& path,const std::string& bytes) {
    const auto current=io::ReadWholeFile(path.toStdString());return current.HasValue()&&current.Value()==bytes;
}
bool MergeEntry(V2MainWindow& w) {
    w.RunCommand("file.new");
    if(!DrawRectangleByHand(w))return false;
    w.RunCommand("output.copy");
    V2OutputTool* output=nullptr;
    for(auto* dialog:w.findChildren<QDialog*>())if(dialog->objectName()=="selectionOutput")output=dynamic_cast<V2OutputTool*>(dialog);
    auto* format=output?output->findChild<QComboBox*>("outputFormat"):nullptr;
    if(!Explain("independent output offers merging with an existing KCD2",format&&format->count()==5))return false;
    format->setCurrentIndex(4);
    auto* choose=output->findChild<QPushButton*>("outputMergeChoose");
    auto* save=output->findChild<QPushButton*>("outputConfirm");
    const bool passed=Explain("merge asks for an existing file before enabling save",choose&&choose->isVisible()&&save&&!save->isEnabled());
    output->close();return passed;
}
bool MergePreservesTarget(V2MainWindow& w) {
    QTemporaryDir dir;if(!dir.isValid()||!MakeSource(w))return false;
    const auto sourcePath=dir.filePath("source.kcd2"),targetPath=dir.filePath("target.kcd2"),mergedPath=dir.filePath("merged.kcd2");
    if(!SaveSource(w,sourcePath))return false;
    const auto source=io::ReadWholeFile(sourcePath.toStdString());if(!source.HasValue())return false;
    io::DocumentFile original;std::string target;if(!MakeTarget(targetPath,original,target))return false;
    const auto before=w.Session().GetDocument().Snapshot();const auto nativeCount=kernel::CachedShapeCount();
    auto* output=OpenMerge(w,targetPath);if(!output)return false;
    auto* confirm=output->findChild<QPushButton*>("outputConfirm");
    if(!Explain("valid existing document enables independent merge",confirm&&confirm->isEnabled()&&output->SaveTo(mergedPath)))return false;
    if(!Explain("merge leaves both original files and source history intact",SameFile(sourcePath,source.Value())&&SameFile(targetPath,target)
        &&w.Session().GetDocument().Revision()==before.revision&&w.Session().GetDocument().Snapshot().id==before.id))return false;
    const auto bytes=io::ReadWholeFile(mergedPath.toStdString());if(!bytes.HasValue())return false;
    const auto merged=io::LoadDocument(bytes.Value());if(!merged.HasValue())return false;
    auto oldPart=merged.Value();const auto& now=oldPart.snapshot;
    if(!Explain("fresh document adds exactly one independent solid",now.id!=before.id&&now.id!=original.snapshot.id
        &&now.entities.size()==original.snapshot.entities.size()+1&&now.features.size()==original.snapshot.features.size()+1))return false;
    const auto imported=now.entities.back().id;const auto feature=now.features.back();
    const auto high=base::DeterministicIdGenerator::CounterOf(imported.Value());
    if(!Explain("new UUIDs advance beyond all target references and have no dependencies",high&&*high>0xf0000
        &&feature.type==domain::FeatureType::FreezeDerived&&feature.inputEntityIds.empty()))return false;
    oldPart.snapshot.entities.pop_back();oldPart.snapshot.features.pop_back();oldPart.snapshot.evaluationOrder=original.snapshot.evaluationOrder;
    oldPart.snapshot.id=original.snapshot.id;oldPart.snapshot.revision=original.snapshot.revision;oldPart.metadata.title=original.metadata.title;
    if(!Explain("all prior definitions groups dimensions metadata and asset bytes survive",io::WriteDocumentJson(oldPart)==io::WriteDocumentJson(original)
        &&oldPart.sideEntries.size()==1&&oldPart.sideEntries.front().data==original.sideEntries.front().data))return false;
    output->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    if(!Explain("reading merge target leaves no native shape owners in source",kernel::CachedShapeCount()==nativeCount))return false;
    if(!w.OpenDocumentFile(mergedPath)||!w.SelectTreeRowForEntity(imported))return false;
    auto* step=V2OutputTool::Open(w,1);const bool exported=step->SaveTo(dir.filePath("merged.step"));step->close();
    if(!Explain("merged native solid reopens and passes strict STEP export",exported&&CountOfKind(w,domain::EntityKind::Part)==2))return false;
    const int wires=CountOfKind(w,domain::EntityKind::Wire);if(!DrawRectangleByHand(w))return false;
    w.RunCommand("edit.undo");if(CountOfKind(w,domain::EntityKind::Wire)!=wires)return false;
    w.RunCommand("edit.redo");
    return Explain("ordinary modeling and history work after opening the merged document",CountOfKind(w,domain::EntityKind::Wire)==wires+1);
}
bool MergeFailureRecovery(V2MainWindow& w) {
    QTemporaryDir dir;if(!dir.isValid()||!MakeSource(w))return false;
    const auto sourcePath=dir.filePath("source.kcd2"),targetPath=dir.filePath("target.kcd2"),outPath=dir.filePath("merged.kcd2");
    if(!SaveSource(w,sourcePath))return false;
    io::DocumentFile original;std::string target;if(!MakeTarget(targetPath,original,target))return false;
    const auto source=io::ReadWholeFile(sourcePath.toStdString());if(!source.HasValue())return false;
    auto* output=OpenMerge(w,targetPath);if(!output)return false;
    const auto revision=w.Session().GetDocument().Revision();
    if(!Explain("merge refuses either original as the output",!output->SaveTo(targetPath)&&!output->SaveTo(sourcePath)
        &&SameFile(sourcePath,source.Value())&&SameFile(targetPath,target)))return false;
    if(!Explain("failed save creates no merged archive and preserves both originals",!output->SaveTo(dir.filePath("missing/merged.kcd2"))
        &&!QFile::exists(outPath)&&SameFile(sourcePath,source.Value())&&SameFile(targetPath,target)))return false;
    auto changed=original;changed.metadata.description="changed on disk";const auto updated=io::SaveDocument(changed);
    if(!updated.HasValue()||!io::WriteFileAtomically(targetPath.toStdString(),updated.Value()).HasValue())return false;
    if(!Explain("stale target cannot overwrite or silently lose a later edit",!output->SaveTo(outPath)&&!QFile::exists(outPath)
        &&SameFile(targetPath,updated.Value())))return false;
    const auto choose=[&](const QString& path){w.SetPathChooser([path](bool){return path;});output->findChild<QPushButton*>("outputMergeChoose")->click();};
    choose(targetPath);if(!output->findChild<QPushButton*>("outputConfirm")->isEnabled())return false;
    choose(QString());
    if(!Explain("cancel clears any previous merge target",!output->SaveTo(outPath)&&!QFile::exists(outPath)))return false;
    choose(targetPath);choose(dir.filePath("missing.kcd2"));
    if(!Explain("failed target read clears any previous merge target",!output->SaveTo(outPath)&&!QFile::exists(outPath)))return false;
    const auto invalid=dir.filePath("invalid.kcd2");if(!io::WriteFileAtomically(invalid.toStdString(),"invalid archive").HasValue())return false;
    choose(invalid);if(output->findChild<QPushButton*>("outputConfirm")->isEnabled())return false;
    choose(targetPath);
    if(!Explain("reselecting an updated target restores merge",output->SaveTo(outPath)&&SameFile(targetPath,updated.Value())))return false;
    output->ChooseFormat(2);
    if(!output->SaveTo(dir.filePath("ordinary-copy.kcd2")))return false;
    output->close();
    return Explain("error recovery and format switching preserve source geometry and history",w.Session().GetDocument().Revision()==revision&&SameFile(sourcePath,source.Value()));
}
}
std::vector<SelfTestCase> OutputMergeCases(){return {{"HP-OUT-MERGE existing document entry",MergeEntry},
    {"HP-OUT-MERGE existing definitions references and native output survive",MergePreservesTarget},
    {"HP-OUT-MERGE originals stale input cancellation and failed output",MergeFailureRecovery}};}
}
