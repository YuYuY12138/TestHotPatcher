#include "G01HotUpdateToolModule.h"
#include "SG01HotUpdatePanel.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"

#define LOCTEXT_NAMESPACE "G01HotUpdateTool"

const FName FG01HotUpdateToolModule::TabName(TEXT("G01HotUpdateTool"));

void FG01HotUpdateToolModule::StartupModule()
{
    // 注册 Tab Spawner，入口在 Window 菜单下
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TabName,
        FOnSpawnTab::CreateRaw(this, &FG01HotUpdateToolModule::OnSpawnTab))
        .SetDisplayName(LOCTEXT("TabTitle", "G01 HotUpdate Tool"))
        .SetTooltipText(LOCTEXT("TabTooltip", "G01 Hot Update Build Tool"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

    UE_LOG(LogTemp, Log, TEXT("[G01HotUpdateTool] Module loaded. Open via Window > G01 HotUpdate Tool"));
}

void FG01HotUpdateToolModule::ShutdownModule()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

TSharedRef<SDockTab> FG01HotUpdateToolModule::OnSpawnTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SG01HotUpdatePanel)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FG01HotUpdateToolModule, G01HotUpdateTool)
