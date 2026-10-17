#pragma once
#include <string_view>
class V2MainWindow;
namespace V2ToolEvidence {
void BeginCase(const char* name);
void EndCase(V2MainWindow& window,bool passed);
class Command {
public:
    Command(V2MainWindow& window,std::string_view id);
    ~Command();
private:
    V2MainWindow& window_;std::string_view id_;bool capture_=false;
};
}
