#pragma once
#include "kachakacha/app/DrawingSession.h"
#include <QWidget>
#include <QVariant>
#include <QStringList>
#include <functional>
namespace v2ui {
// Only a direct new wire/point commit may append tree rows. Undo, edits and file
// replacement continue through the full refresh, without this transient token.
inline void NotifyDrawingCommit(QWidget& view,const kachakacha::v2::app::DrawingSession& session,
    const kachakacha::v2::app::ClickResult& result,const std::function<void()>& callback){
    if(!result.committed||!callback)return;
    QStringList ids;
    for(auto id:result.createdEntityIds){const auto* entity=session.GetDocument().FindEntity(id);
        using K=kachakacha::v2::domain::EntityKind;
        if(!entity||(entity->kind!=K::Wire&&entity->kind!=K::Point)){ids.clear();break;}
        ids.push_back(QString::fromStdString(id.ToString()));}
    auto* root=view.window();const auto previous=root->property("drawingAppendIds");
    root->setProperty("drawingAppendIds",ids);callback();root->setProperty("drawingAppendIds",previous);
}
}
