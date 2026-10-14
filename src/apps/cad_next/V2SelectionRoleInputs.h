#pragma once
class V2MainWindow;
class QDockWidget;
// Explicit roles for the remaining selection-backed editing shelves.
void InstallSelectionRoleInputs(V2MainWindow&, QDockWidget&, int kind);
void RefreshSelectionRoleInputs(V2MainWindow&);
