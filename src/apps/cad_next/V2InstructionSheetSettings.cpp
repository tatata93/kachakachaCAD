#include <QIODevice>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QWidget>
#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QFontComboBox>
#include <QSignalBlocker>
#include <algorithm>
namespace {
QDoubleSpinBox* Number(QWidget* panel,const char* name,double value,double low,double high){
    auto* box=new QDoubleSpinBox(panel);box->setObjectName(name);box->setRange(low,high);box->setDecimals(2);box->setKeyboardTracking(false);box->setSuffix(QStringLiteral(" mm"));box->setValue(value);return box;
}
}
void V2InstructionMode::ShowSheetSettings(){
    auto* panel=new QWidget;settings_=panel;auto* layout=new QVBoxLayout(panel);
    hint_=new QLabel(QStringLiteral("用紙へ場面・画像・文字を配置します。図は3D場面を参照し、場面を直すと更新されます。"),panel);hint_->setWordWrap(true);layout->addWidget(hint_);
    auto& sheet=sheets_[currentSheet_];
    if(activeTool_=="paper"){
        auto* preset=new QComboBox(panel);preset->setObjectName("instructionPaperSize");preset->addItems({"A4","A3","A5","Letter",QStringLiteral("自由サイズ")});layout->addWidget(preset);
        auto* form=new QFormLayout;layout->addLayout(form);auto* width=Number(panel,"instructionPaperWidth",sheet.widthMm,10,2000);
        auto* height=Number(panel,"instructionPaperHeight",sheet.heightMm,10,2000);form->addRow(QStringLiteral("幅"),width);form->addRow(QStringLiteral("高さ"),height);
        const QSizeF sizes[]={{210,297},{297,420},{148,210},{215.9,279.4}};int match=4;
        for(int i=0;i<4;++i)if((sheet.widthMm==sizes[i].width()&&sheet.heightMm==sizes[i].height())||(sheet.heightMm==sizes[i].width()&&sheet.widthMm==sizes[i].height()))match=i;preset->setCurrentIndex(match);
        const auto apply=[this,width,height]{if(loading_)return;before_=Snapshot();auto& paper=sheets_[currentSheet_];paper.widthMm=width->value();paper.heightMm=height->value();Remember();sheetView_->Refresh();};
        connect(width,&QDoubleSpinBox::editingFinished,this,apply);connect(height,&QDoubleSpinBox::editingFinished,this,apply);
        connect(preset,&QComboBox::currentIndexChanged,this,[width,height,apply](int index){const QSizeF values[]={{210,297},{297,420},{148,210},{215.9,279.4}};if(index<0||index>=4)return;width->setValue(values[index].width());height->setValue(values[index].height());apply();});
        auto* rotate=new QPushButton(QStringLiteral("縦・横を切り替え"),panel);rotate->setObjectName("instructionPaperRotate");layout->addWidget(rotate);
        connect(rotate,&QPushButton::clicked,this,[width,height,apply]{const double w=width->value();width->setValue(height->value());height->setValue(w);apply();});
    }
    if(activeTool_=="place_scene"){
        pendingItem_=InstructionSheetItem{};pendingItem_.kind=InstructionItemKind::Scene;pendingItem_.sceneId=scenes_[current_].id;
        auto* source=new QComboBox(panel);source->setObjectName("instructionSceneSource");for(const auto& scene:scenes_)source->addItem(scene.title);source->setCurrentIndex(current_);layout->addWidget(source);
        connect(source,&QComboBox::currentIndexChanged,this,[this](int index){if(index<0||index>=int(scenes_.size()))return;pendingItem_.sceneId=scenes_[index].id;sheetView_->SetTool("place_scene",pendingItem_);});
        hint_->setText(QStringLiteral("配置する場面を選び、用紙をクリック。選択・移動に切り替えて図をダブルクリックすると元の3D場面を編集できます。"));
    }
    if(activeTool_=="text"){
        pendingItem_.kind=InstructionItemKind::Text;pendingItem_.rectMm=QRectF(15,15,80,25);AddFontControls(panel,true);
        auto* text=new QLineEdit(panel);text->setObjectName("instructionText");text->setPlaceholderText(QStringLiteral("文章を入力して用紙をクリック"));layout->addWidget(text);
        const int selected=sheetView_->Selected();if(selected>=0&&sheet.items[selected].kind==InstructionItemKind::Text)text->setText(sheet.items[selected].text);
        pendingItem_.text=text->text();connect(text,&QLineEdit::textEdited,this,[this,text]{pendingItem_.text=text->text();sheetView_->SetTool("text",pendingItem_);});
        auto* apply=new QPushButton(QStringLiteral("選択した文字を更新"),panel);layout->addWidget(apply);
        connect(apply,&QPushButton::clicked,this,[this,text]{const int i=sheetView_->Selected();if(i<0||sheets_[currentSheet_].items[i].kind!=InstructionItemKind::Text)return;before_=Snapshot();sheets_[currentSheet_].items[i].text=text->text();Remember();sheetView_->Refresh();});
    }
    if(activeTool_=="insert_image")hint_->setText(QStringLiteral("画像を用紙の任意位置にクリックして配置。選択・移動でドラッグし、右下の印で大きさを調整できます。"));
    if(activeTool_=="move"){
        auto* targets=new QComboBox(panel);targets->setObjectName("instructionSheetItems");targets->addItem(QStringLiteral("用紙上の対象を選択"));
        for(const auto& item:sheet.items){QString name=item.kind==InstructionItemKind::Text?item.text.left(24):item.kind==InstructionItemKind::Image?QStringLiteral("貼り付け画像"):QStringLiteral("3D場面");
            if(item.kind==InstructionItemKind::Scene)for(const auto& scene:scenes_)if(scene.id==item.sceneId)name=scene.title;
            targets->addItem(QString::number(targets->count())+QStringLiteral("：")+name);}
        targets->setCurrentIndex(sheetView_->Selected()+1);layout->addWidget(targets);
        connect(targets,&QComboBox::currentIndexChanged,this,[this](int i){sheetView_->SetSelected(i-1);});
        const int selected=sheetView_->Selected();if(selected>=0&&selected<int(sheet.items.size())){
            const auto rect=sheet.items[selected].rectMm;auto* form=new QFormLayout;layout->addLayout(form);
            std::vector<QDoubleSpinBox*> values;const char* names[]={"instructionItemX","instructionItemY","instructionItemWidth","instructionItemHeight"};
            const QString labels[]={QStringLiteral("左 mm"),QStringLiteral("上 mm"),QStringLiteral("幅 mm"),QStringLiteral("高さ mm")};const double coords[]={rect.x(),rect.y(),rect.width(),rect.height()};
            for(int i=0;i<4;++i){auto* box=Number(panel,names[i],coords[i],i<2?-100000:.01,100000);values.push_back(box);form->addRow(labels[i],box);}
            auto* apply=new QPushButton(QStringLiteral("位置・大きさを適用"),panel);apply->setObjectName("instructionApplyItem");layout->addWidget(apply);
            connect(apply,&QPushButton::clicked,this,[this,selected,values]{before_=Snapshot();sheets_[currentSheet_].items[selected].rectMm=QRectF(values[0]->value(),values[1]->value(),values[2]->value(),values[3]->value());Remember();sheetView_->Refresh();});
        }
        hint_->setText(QStringLiteral("クリックで選択、ドラッグで移動。右下の印でサイズ変更、Shiftで縦横比を維持。重なった図は一覧からも選べます。"));
    }
    if(activeTool_=="image")AddImageExportControls(panel);
    layout->addStretch();window_.OperationHost().ShowTemporaryPage(panel,QStringLiteral("用紙編集 — ")+sheet.title);
}
void V2InstructionMode::InsertImage(){
    const auto path=pathChooser_?pathChooser_(false):QFileDialog::getOpenFileName(&window_,QStringLiteral("用紙へ貼る画像"),{},QStringLiteral("画像 (*.png *.jpg *.jpeg)"));
    if(path.isEmpty())return;QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>64*1024*1024){if(hint_)hint_->setText(QStringLiteral("画像を開けません。64 MB以下のPNG/JPEGを選んでください。"));return;}
    const auto bytes=file.readAll();QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);
    const auto format=reader.format().toLower();const auto dimensions=reader.size();
    if((format!="png"&&format!="jpeg"&&format!="jpg")||!dimensions.isValid()||qint64(dimensions.width())*dimensions.height()>16000000){if(hint_)hint_->setText(QStringLiteral("PNG/JPEG、1600万画素以下の画像を選んでください。"));return;}
    const auto image=reader.read();if(image.isNull()){if(hint_)hint_->setText(QStringLiteral("画像のデータを読み込めません。"));return;}
    pendingItem_=InstructionSheetItem{};pendingItem_.kind=InstructionItemKind::Image;pendingItem_.image=image;pendingItem_.imageBytes=bytes;pendingItem_.imageFormat=QString::fromLatin1(format);
    const auto& sheet=sheets_[currentSheet_];
    const double scale=std::min({80.0/image.width(),sheet.widthMm/image.width(),sheet.heightMm/image.height()});
    pendingItem_.rectMm=QRectF(15,15,std::clamp(image.width()*scale,.01,sheet.widthMm),
        std::clamp(image.height()*scale,.01,sheet.heightMm));
    activeTool_="insert_image";ShowSettings();sheetView_->SetTool("insert_image",pendingItem_);
}
