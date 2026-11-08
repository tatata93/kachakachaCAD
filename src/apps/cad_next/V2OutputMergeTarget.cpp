#include "V2OutputMergeTarget.h"
#include "V2GeometryJob.h"
#include "V2PreparedDocument.h"
#include "kachakacha/io/AtomicFile.h"
#include <QFileInfo>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
using namespace kachakacha::v2;

V2OutputMergeTarget::V2OutputMergeTarget(QWidget* parent,std::function<QString()> choose,std::function<void()> changed)
    :QWidget(parent),choose_(std::move(choose)),changed_(std::move(changed)) {
    setObjectName("outputMergeTarget");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto* button=new QPushButton(QStringLiteral("組み合わせるKCD2を選ぶ…"),this);
    button->setObjectName("outputMergeChoose");layout->addWidget(button);
    status_=new QLabel(QStringLiteral("既存ファイルを選んでください。両方の原本は変更せず、別名で保存します。"),this);
    status_->setObjectName("outputMergeStatus");status_->setWordWrap(true);layout->addWidget(status_);
    QObject::connect(button,&QPushButton::clicked,this,[this]{Choose();});
}
QString V2OutputMergeTarget::Description() const {
    return Ready()?QStringLiteral("%1（既存%2項目）").arg(QFileInfo(path_).fileName()).arg(file_->snapshot.entities.size()):QString();
}
void V2OutputMergeTarget::Choose() {
    file_.reset();archive_.clear();path_.clear();changed_();
    const auto chosen=choose_();
    if(chosen.isEmpty()){status_->setText(QStringLiteral("ファイル選択を取り消しました。組み合わせるKCD2を選んでください。"));return;}
    const auto path=QFileInfo(chosen).absoluteFilePath();
    const auto original=io::ReadWholeFile(path.toStdString());
    if(!original.HasValue()){status_->setText(QString::fromStdString(original.FirstMessageJa()));return;}
    std::optional<io::DocumentFile> file;
    const auto ready=V2GeometryJob::PrepareDocument(this,path,[&](const QString& dataPath,const QString& token,const std::function<bool()>& pulse){
        auto result=ReadPreparedDocument(dataPath,token,pulse);
        if(!result.HasValue())return base::Result<bool>::Failure(result.Diagnostics());
        if(result.Value()->archive!=original.Value())return base::Result<bool>::Failure(base::MakeError("EXP-P004","読込中にファイルが変わりました。選び直してください。"));
        const auto loaded=io::LoadDocument(result.Value()->archive);
        if(!loaded.HasValue())return base::Result<bool>::Failure(loaded.Diagnostics());
        file=loaded.Value();return base::Result<bool>::Success(true);
    });
    if(!ready.HasValue()){status_->setText(QString::fromStdString(ready.FirstSummaryJa()));return;}
    path_=path;archive_=original.Value();file_=std::move(file);
    status_->setText(Description()+QStringLiteral("\n元の履歴・グループ・画像を残し、選択形状のコピーを追加します。プレビューは追加分です。"));
    changed_();
}
bool V2OutputMergeTarget::CheckUnchanged(QString& error) const {
    if(!Ready()){error=QStringLiteral("組み合わせるKCD2を選んでください。");return false;}
    const auto current=io::ReadWholeFile(path_.toStdString());
    if(!current.HasValue()||current.Value()!=archive_){error=QStringLiteral("選んだKCD2が更新または削除されています。ファイルを選び直してください。");return false;}
    return true;
}
