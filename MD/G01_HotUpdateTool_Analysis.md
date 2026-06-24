# G01 热更新出包面板 — 项目现状分析与接入方案

> 日期：2026-06-23
> 状态：分析阶段，未开始实现

---

## 1. HotPatcher 集成现状

**已集成**，插件位于 `Plugins/HotPatcher/`。

模块结构：

| 模块 | 类型 | LoadingPhase | 我们需要依赖 | 作用 |
|------|------|-------------|-------------|------|
| HotPatcherRuntime | Runtime | PreDefault | ✅ 必须 | Settings 结构体、版本数据、JSON 序列化工具 |
| HotPatcherCore | Editor | PreDefault | ✅ 必须 | UReleaseProxy、UPatcherProxy、Commandlet、工具函数 |
| HotPatcherEditor | Editor | Default | ❌ 不需要 | 原始 Slate UI（我们要替代它） |
| BinariesPatchFeature | Runtime | PreDefault | ❌ 不需要 | 二进制差分（当前不用） |
| CmdHandler | Developer | PostConfigInit | ❌ 不需要 | DDC/Shader 环境配置 |

我们的 Adapter 和 Commandlet 只需要依赖 `HotPatcherRuntime` + `HotPatcherCore`。

---

## 2. ByRelease / ByPatch 背后的调用链

### ByRelease（导出基础版本快照）

```
SHotPatcherReleaseWidget::DoExportRelease()
  │
  ├─ 非独立进程模式：
  │    UReleaseProxy* Proxy = NewObject<UReleaseProxy>();
  │    Proxy->Init(FExportReleaseSettings*);
  │    Proxy->DoExport();
  │
  └─ 独立进程模式（默认）：
       序列化 FExportReleaseSettings → JSON 文件
       启动 UE-Cmd.exe -run=HotRelease -config=<json>
```

UReleaseProxy::DoExport() 内部执行 7 个 Worker：
SaveConfig → ImportPakList → ImportProjectSettings → ExportNewRelease（扫描资产+外部文件） → SaveReleaseVersion（输出 xxx_Release.json） → BackupMetadata → ReleaseSummary

### ByPatch（生成补丁包）

```
SHotPatcherPatchWidget::DoExportPatch()
  │
  ├─ 非独立进程模式：
  │    UPatcherProxy* Proxy = NewObject<UPatcherProxy>();
  │    Proxy->Init(FExportPatchSettings*);
  │    Proxy->DoExport();
  │
  └─ 独立进程模式（默认）：
       序列化 FExportPatchSettings → JSON 文件
       启动 UE-Cmd.exe -run=HotPatcher -config=<json>
```

UPatcherProxy::DoExport() 内部通过 FHotPatcherPatchContext 驱动：
版本对比 → 资产 Cook → UnrealPak 打包 → Shader/AssetRegistry 处理 → 输出 pak + PakCommands.txt + PakFilesInfo.json

---

## 3. 能否直接创建并调用 Proxy

**可以。** 两个 Proxy 都是 UObject，有 HOTPATCHERCORE_API 导出，公开 API 清晰。

### 调用模式

```cpp
// Release
#include "CreatePatch/ReleaseProxy.h"
#include "CreatePatch/FExportReleaseSettings.h"

FExportReleaseSettings* Settings = new FExportReleaseSettings();
// ... 填充字段 ...
UReleaseProxy* Proxy = NewObject<UReleaseProxy>();
Proxy->AddToRoot();  // 防止 GC
Proxy->Init(Settings);
Proxy->OnPaking.AddLambda([](FString Msg) { /* 进度回调 */ });
Proxy->OnShowMsg.AddLambda([](FString Msg) { /* 消息回调 */ });
bool bSuccess = Proxy->DoExport();
Proxy->RemoveFromRoot();
```

```cpp
// Patch
#include "CreatePatch/PatcherProxy.h"
#include "CreatePatch/FExportPatchSettings.h"

FExportPatchSettings* Settings = new FExportPatchSettings();
// ... 填充字段（包括 BaseVersion 路径）...
UPatcherProxy* Proxy = NewObject<UPatcherProxy>();
Proxy->AddToRoot();
Proxy->Init(Settings);
bool bSuccess = Proxy->DoExport();
Proxy->RemoveFromRoot();
```

### 需要的模块依赖（Build.cs）

```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "HotPatcherRuntime",  // Settings 结构体、JSON 工具
    "HotPatcherCore",     // UReleaseProxy、UPatcherProxy、Helper
    "Json",
    "JsonUtilities",
});
```

### 需要的头文件

```cpp
#include "CreatePatch/FExportReleaseSettings.h"
#include "CreatePatch/FExportPatchSettings.h"
#include "CreatePatch/ReleaseProxy.h"
#include "CreatePatch/PatcherProxy.h"
#include "FlibHotPatcherCoreHelper.h"
#include "Templates/HotPatcherTemplateHelper.hpp"  // JSON 序列化
```

### JSON 加载/保存

```cpp
// 从 JSON 文件加载配置
FString JsonContent;
FFileHelper::LoadFileToString(JsonContent, *JsonPath);
THotPatcherTemplateHelper::TDeserializeJsonStringAsStruct(JsonContent, *Settings);

// 保存配置到 JSON
FString OutJson;
THotPatcherTemplateHelper::TSerializeStructAsJsonString(*Settings, OutJson);
FFileHelper::SaveStringToFile(OutJson, *OutputPath);
```

---

## 4. HotPatcher 已有的 Commandlet

**有，且可以直接复用。**

| Commandlet | 命令 | 核心参数 | 内部实现 |
|------------|------|---------|---------|
| UHotReleaseCommandlet | `-run=HotRelease` | `-config=<json路径>` | 创建 UReleaseProxy → Init → DoExport |
| UHotPatcherCommandlet | `-run=HotPatcher` | `-config=<json路径>` | 创建 UPatcherProxy → Init → DoExport |

两个 Commandlet 都支持命令行覆盖任意 Settings 字段：`-PropertyName=Value`。
也支持追加平台：`-AddPatchPlatforms=Android`、`-TargetPlatformsOverride=Android`。

---

## 5. 是否需要自己实现 G01HotPatchCommandlet

**需要，但不是从零写。**

HotPatcher 自带的 Commandlet 只负责"执行 Release/Patch"这一步，缺少以下能力：

| 缺失能力 | 说明 |
|---------|------|
| 构建任务描述 | 不理解 BuildTask.json（平台、补丁类型、快照/增量策略） |
| Release + Patch 联动 | 只能单独跑 Release 或 Patch，不能一键完成整个链路 |
| MD5/Size 计算 | 不会计算产物的 MD5 和文件大小 |
| VersionManifest 生成 | 不会生成服务端需要的版本清单 JSON |
| BuildReport 生成 | 不会记录本次构建的元信息（时间、版本号、产物路径） |
| 产物目录规范化 | 产物目录由 HotPatcher 内部命名，不受我们控制 |
| 历史记录追加 | 不会维护本地的构建历史 |

**建议**：实现 `UG01HotPatchCommandlet`，读取 `BuildTask.json`，内部调用 Adapter，Adapter 再调用 HotPatcher 的 Proxy/Commandlet。这样：
- Editor 面板调用 → G01Commandlet
- CI/CD 调用 → 同一个 G01Commandlet
- 构建逻辑只写一份

---

## 6. 当前已有的配置/产物/记录

| 项目 | 状态 | 路径 |
|------|------|------|
| Release 配置模板 | ✅ 有 | `ReleaseTest.json`（根目录） |
| Patch 配置模板 | ✅ 有 | `PatchTest.json`（根目录） |
| Release 产物 | 部分有 | `Saved/HotPatcher/{版本号}/{版本号}_Release.json` |
| Patch 产物 | 部分有 | `Saved/HotPatcher/{版本号}/Android/*.pak` |
| VersionManifest | ❌ 无 | 需要自建 |
| BuildReport | ❌ 无 | 需要自建 |
| 构建历史记录 | ❌ 无 | 需要自建 |
| MD5 计算 | ❌ 无 | Patch 产物的 PakFilesInfo.json 有文件列表但无 MD5 |
| push_patch.bat | ✅ 有 | 根目录，推送到 Android Pending 目录 |

---

## 7. Adapter 模块放置建议

**新建一个 Editor 模块：`G01HotUpdateTool`**

```
Plugins/G01HotUpdateTool/
├── G01HotUpdateTool.uplugin
├── Source/
│   ├── G01HotUpdateTool/          ← Editor 模块（面板 UI）
│   │   ├── G01HotUpdateTool.Build.cs
│   │   ├── Public/
│   │   │   ├── SG01HotUpdatePanel.h        ← Slate 面板
│   │   │   ├── G01HotPatcherAdapter.h      ← Adapter 封装层
│   │   │   ├── G01BuildTask.h              ← BuildTask 数据结构
│   │   │   ├── G01BuildHistory.h           ← 构建历史管理
│   │   │   └── G01VersionManifest.h        ← Manifest 生成
│   │   └── Private/
│   │       ├── SG01HotUpdatePanel.cpp
│   │       ├── G01HotPatcherAdapter.cpp
│   │       ├── G01BuildTask.cpp
│   │       ├── G01BuildHistory.cpp
│   │       ├── G01VersionManifest.cpp
│   │       └── G01HotUpdateToolModule.cpp  ← 模块入口，注册菜单/Tab
│   │
│   └── G01HotPatchCommandlet/     ← 独立模块（可被 CI 调用，不依赖 Slate）
│       ├── G01HotPatchCommandlet.Build.cs
│       ├── Public/
│       │   └── G01HotPatchCommandlet.h
│       └── Private/
│           └── G01HotPatchCommandlet.cpp

放在项目 Plugins 下而不是 Source 下的原因：
- 作为独立插件可以跨项目复用
- 不会污染项目的游戏模块
- 模块类型设为 Editor/DeveloperTool，不会打进游戏包
```

为什么不放在 `Source/G01/` 下：
- 热更工具是 Editor Only，不应和游戏运行时代码混在一起
- 作为独立插件后续可以直接拷到 G01 主项目使用

---

## 8. BuildTask.json 字段结构建议

```json
{
    "taskType": "Patch",
    "platform": "Android",
    "patchType": "Snapshot",
    
    "baseVersion": "1.0.0",
    "targetVersion": "1.0.6",
    
    "releaseConfigTemplate": "[PROJECTDIR]/ReleaseTest.json",
    "patchConfigTemplate": "[PROJECTDIR]/PatchTest.json",
    
    "outputDir": "[PROJECTDIR]/Saved/HotPatcher/",
    
    "options": {
        "bCookPatchAssets": true,
        "bCompressPak": true,
        "bCalculateMD5": true,
        "bGenerateManifest": true,
        "bGenerateBuildReport": true,
        "bStandaloneMode": true
    }
}
```

字段说明：

| 字段 | 说明 | 谁填 |
|------|------|------|
| taskType | `Release` 或 `Patch` | 面板根据操作自动设置 |
| platform | 目标平台 | 用户从下拉框选 |
| patchType | `Snapshot`（从基础包出全量）或 `Incremental`（从上一版本出增量） | 用户选择 |
| baseVersion | 基准版本号，Snapshot 时为基础包版本，Incremental 时为上一版本 | 面板根据 patchType 自动推断，用户可覆盖 |
| targetVersion | 目标版本号 | 面板自动递增，用户可修改 |
| releaseConfigTemplate | Release 配置模板路径 | 默认值，一般不需要改 |
| patchConfigTemplate | Patch 配置模板路径 | 默认值，一般不需要改 |
| outputDir | 产物输出根目录 | 默认值，一般不需要改 |
| options | 构建选项 | 面板提供勾选框 |

---

## 9. 推荐接入方案：调用链路

```
┌─────────────────────────┐
│  G01 Editor 面板         │
│  (SG01HotUpdatePanel)   │
│                         │
│  · 选平台、补丁类型      │
│  · 选/确认版本号         │
│  · 查看历史记录          │
│  · 点击"一键出包"        │
└────────────┬────────────┘
             │ 生成 BuildTask.json
             │ 启动独立进程
             ▼
┌─────────────────────────┐
│  G01HotPatchCommandlet  │
│  (-run=G01HotPatch)     │
│                         │
│  · 读取 BuildTask.json  │
│  · 调用 Adapter          │
│  · 生成 Manifest         │
│  · 生成 BuildReport      │
│  · 计算 MD5/Size         │
└────────────┬────────────┘
             │ 调用稳定接口
             ▼
┌─────────────────────────┐
│  G01HotPatcherAdapter   │
│                         │
│  · ExportRelease()      │
│  · BuildPatch()         │
│                         │
│  内部：                  │
│  · 加载配置模板 JSON     │
│  · 填充版本号/平台/路径  │
│  · 创建 UReleaseProxy   │
│    或 UPatcherProxy     │
│  · 调 Init() + DoExport │
│  · 收集产物路径          │
└─────────────────────────┘

CI/CD 也可以直接调用：
  UE-Cmd.exe <Project> -run=G01HotPatch -config=BuildTask.json
```

**为什么不让面板直接调 Proxy**：
- Cook 过程重（几分钟~几十分钟），直接在 Editor 进程内跑会卡死 UI，且 Cook 失败可能导致 Editor 崩溃
- 独立进程模式下 Editor 可以显示进度、支持取消、不影响编辑器稳定性
- Commandlet 可以被 CI/CD 直接调用，不需要打开 Editor
- 面板 → Commandlet → Adapter → Proxy 这条链路，每一层职责单一，任何一层替换不影响其他层

---

## 10. MVP 实现步骤

### Phase 1：Adapter + Commandlet（无 UI，命令行可用）

1. 创建 `G01HotUpdateTool` 插件骨架（uplugin + 两个模块的 Build.cs）
2. 实现 `G01HotPatcherAdapter`：
   - `ExportRelease(Version, Platform, ConfigTemplate) → bool + 产物路径`
   - `BuildPatch(BaseVersion, TargetVersion, Platform, PatchType, ConfigTemplate) → bool + 产物路径`
   - 内部：加载模板 JSON → 填充参数 → 创建 Proxy → DoExport
3. 实现 `G01HotPatchCommandlet`：
   - 读取 BuildTask.json → 调用 Adapter → 计算 MD5 → 生成 Manifest → 写 BuildReport
4. 定义 BuildTask.json 和 BuildReport.json 的数据结构
5. 命令行验证：`UE-Cmd.exe <Project> -run=G01HotPatch -config=BuildTask.json`

**验证标准**：能通过命令行一行命令完成"Release → Patch → 产物 + Manifest + Report"。

### Phase 2：Editor 面板（可视化操作）

6. 实现 `SG01HotUpdatePanel`：
   - 平台下拉框、补丁类型选择、版本号输入
   - "一键出包"按钮 → 生成 BuildTask.json → 启动 G01HotPatchCommandlet
   - 进度显示（监听独立进程输出）
7. 实现 `G01BuildHistory`：
   - 扫描 Saved/HotPatcher/ 下已有的 Release/Patch 产物
   - 读取 BuildReport.json 展示历史记录
   - 面板里用 ListView 展示

**验证标准**：策划打开面板 → 选平台 → 选补丁类型 → 点一键出包 → 等待完成 → 看到历史记录。

### Phase 3：策划体验优化

8. 版本号自动递增（读取历史记录，建议下一个版本号）
9. 参数校验（版本号格式、基准版本是否存在、平台是否有效）
10. push_patch.bat 集成到面板（一键推送到手机）
11. 产物目录一键打开（Explorer）

---

## 产物目录结构建议

```
Saved/HotPatcher/
├── Releases/
│   ├── 1.0.0/
│   │   ├── 1.0.0_Release.json          ← HotPatcher 生成的 Release 快照
│   │   └── BuildReport_1.0.0.json      ← G01 生成的构建报告
│   └── 1.1.0/
│       └── ...
│
├── Patches/
│   ├── 1.0.1/
│   │   ├── Android/
│   │   │   └── 1.0.1_Android_001_P.pak  ← HotPatcher 生成的补丁 Pak
│   │   ├── PakCommands.txt              ← HotPatcher 生成
│   │   ├── VersionManifest_1.0.1.json   ← G01 生成的版本清单
│   │   └── BuildReport_1.0.1.json       ← G01 生成的构建报告
│   └── 1.0.2/
│       └── ...
│
├── BuildHistory.json                     ← 所有构建记录的索引文件
│
└── Templates/
    ├── ReleaseConfig.json                ← Release 配置模板
    └── PatchConfig.json                  ← Patch 配置模板
```

---

## VersionManifest.json 结构建议

```json
{
    "version": "1.0.6",
    "baseVersion": "1.0.0",
    "patchType": "Snapshot",
    "platform": "Android",
    "buildTime": "2026-06-23T15:30:00Z",
    "files": [
        {
            "name": "1.0.6_Android_001_P.pak",
            "url": "",
            "size": 15728640,
            "md5": "a1b2c3d4e5f6..."
        }
    ],
    "releaseNote": ""
}
```

url 字段留空，上传 CDN 后由服务端填充。

---

## BuildReport.json 结构建议

```json
{
    "taskType": "Patch",
    "platform": "Android",
    "patchType": "Snapshot",
    "baseVersion": "1.0.0",
    "targetVersion": "1.0.6",
    "buildTime": "2026-06-23T15:30:00Z",
    "duration": 45.2,
    "success": true,
    "outputs": [
        {
            "type": "Pak",
            "path": "Saved/HotPatcher/Patches/1.0.6/Android/1.0.6_Android_001_P.pak",
            "size": 15728640,
            "md5": "a1b2c3d4e5f6..."
        },
        {
            "type": "Manifest",
            "path": "Saved/HotPatcher/Patches/1.0.6/VersionManifest_1.0.6.json"
        }
    ],
    "changedAssets": 12,
    "changedExternFiles": 3,
    "errors": []
}
```
