#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2FabricationDock.h"
#include "V2OutputTool.h"
#include "kachakacha/app/Selection.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QPixmap>
#include <QString>
#include <iostream>

namespace kachakacha::v2::selftest {
bool CheckPracticalFreezeSweep(V2MainWindow& w,const QString& input,const QString& directory)
{
    for(const int percent:{0,30,50,100})for(const int format:{1,2}) {
        const auto stem=directory+QString("/freeze-%1-%2").arg(percent).arg(format==1?"parts":"both");
        std::cout<<"PRACTICAL FREEZE "<<percent<<" output="<<format<<std::endl;
        if(!w.OpenDocumentFile(input)||!w.RebuildProblems().isEmpty())return false;
        auto models=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::FabricationModel);
        if(models.entityIds.empty())return false;
        w.Viewport().SetSelection(models);auto& dock=w.FabricationDock();
        dock.SetPartNumbersText(QStringLiteral("すべて"));
        dock.TypeAssemblyPercent(percent);dock.PressApplyAssembly();
        auto* output=w.findChild<QComboBox*>("fabricationGenerationOutput");
        if(!output)return false;output->setCurrentIndex(format);
        const int before=CountOfKind(w,domain::EntityKind::Part);
        const int wires=CountOfKind(w,domain::EntityKind::Wire);
        w.RunCommand("fabrication.freeze_state");
        const int after=CountOfKind(w,domain::EntityKind::Part);
        if(!Explain("freeze creates parts at selected assembly state",after>before)) {
            Note(w.StatusText().toUtf8().constData());return false;
        }
        if(format==2&&!Explain("Both also creates editable wires",CountOfKind(w,domain::EntityKind::Wire)>wires))return false;
        w.RunCommand("edit.undo");
        if(!Explain("one undo removes frozen parts",CountOfKind(w,domain::EntityKind::Part)==before))return false;
        w.RunCommand("edit.redo");
        if(!Explain("redo restores all frozen parts",CountOfKind(w,domain::EntityKind::Part)==after))return false;
        auto parts=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::Part);
        w.Viewport().SetSelection(parts);auto* tool=V2OutputTool::Open(w,0);
        bool ok=tool->SaveTo(stem+".stl");tool->ChooseFormat(1);
        ok=tool->SaveTo(stem+".step")&&ok;tool->close();if(!ok)return false;
        w.SetPathChooser([stem](bool){return stem+".kcd2";});w.RunCommand("file.save_as");
        if(!QFile::exists(stem+".kcd2"))return false;
        w.RunCommand("file.new");
        if(!w.OpenDocumentFile(stem+".kcd2")||!w.RebuildProblems().isEmpty())return false;
        if(!Explain("saved freeze restores part count",CountOfKind(w,domain::EntityKind::Part)==after))return false;
        w.Viewport().SetViewDirection(ViewDirection::Isometric);w.Viewport().FitToDocument();
        QApplication::processEvents();w.grab().save(stem+".png");
    }
    return true;
}
}
