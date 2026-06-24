# G01 HotPatch Commandlet MVP — 实施方案

> Phase 1：无 UI，命令行可用
> 目标：验证 BuildTask.json → Commandlet → Adapter → HotPatcher → 产物 的完整链路

---

## 1. 新增文件清单

```
Plugins/G01HotUpdateTool/
├── G01HotUpdateTool.uplugin
│
└── Source/G01HotUpdateTool/
    ├── G01HotUpdateTool.Build.cs
    │
    ├── Public/
    │   ├── G01BuildTask.h                ← BuildTask 数据结构（USTRUCT）
    │   ├── G01HotPatcherAdapter.h        ← Adapter：封装 HotPatcher 调用
    │   ├── G01VersionManifest.h          ← Manifest 数据结构 + 生成逻辑
    │   ├── G01BuildReport.h              ← BuildReport 数据结构 + 生成逻辑
    │   └── G01HotPatchCommandlet.h       ← Commandlet：统一构建入口
    │
    ├── Private/
    │   ├── G01BuildTask.cpp
    │   ├── G01HotPatcherAdapter.cpp
    │   ├── G01VersionManifest.cpp
    │   ├── G01BuildReport.cpp
    │   ├── G01HotPatchCommandlet.cpp
    │   └── G01HotUpdateToolModule.cpp    ← 模块入口（MVP 阶段只注册 Commandlet）
    │
    └── Templates/
        └── BuildTask_Example.json        ← 示例 BuildTask
```

共 12 个文件。其中 6 个 .h/.cpp 对（5 个类 + 1 个模块入口）。

---

## 2. 模块设计

### uplugin

```json
{
    "Modules": [
        {
            "Name": "G01HotUpdateTool",
            "Type": "Editor",
            "LoadingPhase": "Default"
        }
    ],
    "Plugins": [
        { "Name": "HotPatcher", "Enabled": true }
    ]
}
```

Type 设为 Editor：
- Commandlet 在 UE-Cmd.exe 下可以加载 Editor 模块
- 不会打进游戏包
- 可以依赖 HotPatcherCore（也是 Editor 模块）

### Build.cs 依赖

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
});

PrivateDependencyModuleNames.AddRange(new string[] {
    "HotPatcherRuntime",    // FExportReleaseSettings, FExportPatchSettings, JSON 工具
    "HotPatcherCore",       // UReleaseProxy, UPatcherProxy
    "Json",
    "JsonUtilities",
    "PakFile",              // MD5 计算时读取 pak 文件
});
```

### 潜在依赖问题

| 问题 | 风险 | 应对 |
|------|------|------|
| HotPatcherCore 是 Editor 模块 | 低 | 我们的模块也是 Editor，可以依赖 |
| Commandlet 需要 Editor 模块 | 低 | UE-Cmd.exe 加载 Editor 模块没问题，HotPatcher 自己的 Commandlet 也是这么做的 |
| Android Cook 需要 SDK 环境 | 中 | 需要 ANDROID_HOME/NDKROOT 环境变量，和手动打包一样 |
| HotPatcher 内部 API 变化 | 低 | Adapter 层隔离，只有 Adapter 内部需要改 |

---

## 3. BuildTask.json 示例

```json
{
    "taskType": "Patch",
    "platform": "Android",
    "patchType": "Snapshot",

    "releaseVersion": "1.0.0",
    "targetVersion": "1.0.6",

    "releaseConfigTemplate": "ReleaseTest.json",
    "patchConfigTemplate": "PatchTest.json",

    "outputDir": "Saved/HotPatcher",

    "options": {
        "bCookPatchAssets": true,
        "bCompressPak": true,
        "bStandaloneMode": false,
        "bCalculateMD5": true,
        "bGenerateManifest": true,
        "bGenerateBuildReport": true
    }
}
```

字段说明：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| taskType | string | 是 | `Release`（只出 Release）/ `Patch`（出 Release + Patch）/ `PatchOnly`（只出 Patch，需已有 Release） |
| platform | string | 是 | `Android` / `Windows` / `iOS` |
| patchType | string | Patch 时必填 | `Snapshot`（基于基础包全量）/ `Incremental`（基于上一版本增量） |
| releaseVersion | string | 是 | Release 基准版本号（Snapshot 模式下就是基础包版本） |
| targetVersion | string | Patch 时必填 | 补丁目标版本号 |
| releaseConfigTemplate | string | 是 | Release 配置模板路径（相对项目根目录） |
| patchConfigTemplate | string | Patch 时必填 | Patch 配置模板路径 |
| outputDir | string | 是 | 产物输出根目录（相对项目根目录） |
| options.bStandaloneMode | bool | 否 | 默认 false（Commandlet 本身已经是独立进程，内部不需要再起子进程） |
| options.bCalculateMD5 | bool | 否 | 默认 true |
| options.bGenerateManifest | bool | 否 | 默认 true |
| options.bGenerateBuildReport | bool | 否 | 默认 true |

注意 `bStandaloneMode` 在 Commandlet 内部应设为 false：因为 Commandlet 本身已经在独立的 UE-Cmd 进程里运行了，再起子进程没有意义，直接调 Proxy 更高效。

---

## 4. Commandlet 执行伪代码

```
UG01HotPatchCommandlet::Main(Params)
{
    // ========== 1. 读取并校验 BuildTask ==========
    解析 -config=<path> 参数
    读取 BuildTask.json → FG01BuildTask 结构体
    校验：taskType 合法、platform 合法、版本号格式正确、模板文件存在

    // ========== 2. 准备输出目录 ==========
    ReleasesDir = OutputDir/Releases/{releaseVersion}/
    PatchesDir  = OutputDir/Patches/{targetVersion}/

    // ========== 3. 处理 Release ==========
    if (taskType == "Release" || taskType == "Patch")
    {
        // 检查该 Release 是否已存在
        ReleaseJsonPath = ReleasesDir/{releaseVersion}_Release.json
        if (已存在 && taskType == "Patch")
        {
            Log("Release {releaseVersion} already exists, skipping")
        }
        else
        {
            // 调 Adapter 导出 Release
            Adapter.ExportRelease(
                Version     = releaseVersion,
                Platform    = platform,
                Template    = releaseConfigTemplate,
                OutputDir   = ReleasesDir
            )
            if (失败) → 报错退出
        }
    }

    // ========== 4. 处理 Patch ==========
    if (taskType == "Patch" || taskType == "PatchOnly")
    {
        // 确定 BaseVersion
        if (patchType == "Snapshot")
            baseVersion = releaseVersion      // 从基础包出全量
        else if (patchType == "Incremental")
            baseVersion = 上一个版本号         // 需要从历史记录或文件系统推断

        // 确定 BaseVersion 的 Release JSON 路径
        baseReleaseJson = OutputDir/Releases/{baseVersion}/{baseVersion}_Release.json
        if (不存在) → 报错退出

        // 调 Adapter 构建 Patch
        PakOutputPaths = Adapter.BuildPatch(
            BaseVersion     = baseVersion,
            TargetVersion   = targetVersion,
            Platform        = platform,
            Template        = patchConfigTemplate,
            BaseReleaseJson = baseReleaseJson,
            OutputDir       = PatchesDir
        )
        if (失败) → 报错退出
    }

    // ========== 5. 后处理：MD5 + Manifest + Report ==========
    if (bCalculateMD5)
    {
        遍历 PatchesDir 下所有 .pak 文件
        for each pak:
            MD5 = 流式读取计算（每次 4MB）
            Size = 文件大小
            记录到 FileInfoList
    }

    if (bGenerateManifest)
    {
        构造 FG01VersionManifest:
            version = targetVersion
            baseVersion = baseVersion
            patchType = patchType
            platform = platform
            buildTime = 当前时间
            files = FileInfoList（name, size, md5, url=""）
        
        序列化为 JSON → PatchesDir/VersionManifest_{targetVersion}.json
    }

    if (bGenerateBuildReport)
    {
        构造 FG01BuildReport:
            所有 BuildTask 参数 + 执行结果 + 产物路径 + 耗时 + 错误信息
        
        序列化为 JSON → PatchesDir/BuildReport_{targetVersion}.json
    }

    // ========== 6. 追加构建历史 ==========
    读取 OutputDir/BuildHistory.json（不存在则创建）
    追加本次构建记录
    保存

    Log("Build complete: {targetVersion}")
    return 0
}
```

---

## 5. Adapter 调用 HotPatcher 的方式

### 接口设计

```cpp
class G01HOTUPDATETOOL_API FG01HotPatcherAdapter
{
public:
    /**
     * 导出 Release（基础版本快照）
     *
     * @param Version       版本号，如 "1.0.0"
     * @param Platform      目标平台，如 "Android"
     * @param TemplatePath  Release 配置模板 JSON 路径
     * @param OutputDir     输出目录
     * @return              true=成功，产物在 OutputDir/{Version}_Release.json
     */
    static bool ExportRelease(
        const FString& Version,
        const FString& Platform,
        const FString& TemplatePath,
        const FString& OutputDir
    );

    /**
     * 构建 Patch（补丁包）
     *
     * @param BaseVersion       基准版本号
     * @param TargetVersion     目标版本号
     * @param Platform          目标平台
     * @param TemplatePath      Patch 配置模板 JSON 路径
     * @param BaseReleaseJson   基准版本的 Release JSON 文件绝对路径
     * @param OutputDir         输出目录
     * @param OutPakPaths       [out] 生成的 pak 文件路径列表
     * @return                  true=成功
     */
    static bool BuildPatch(
        const FString& BaseVersion,
        const FString& TargetVersion,
        const FString& Platform,
        const FString& TemplatePath,
        const FString& BaseReleaseJson,
        const FString& OutputDir,
        TArray<FString>& OutPakPaths
    );
};
```

### 内部实现伪代码

```
ExportRelease(Version, Platform, Template, OutputDir)
{
    // 1. 加载模板
    FExportReleaseSettings Settings;
    LoadJsonToStruct(Template, Settings);

    // 2. 覆盖关键字段
    Settings.VersionId = Version;
    Settings.SavePath = OutputDir;
    Settings.bStandaloneMode = false;   // Commandlet 已经是独立进程
    Settings.bNoShaderCompile = true;

    // 3. 调用 HotPatcher Proxy
    UReleaseProxy* Proxy = NewObject<UReleaseProxy>();
    Proxy->AddToRoot();
    Proxy->Init(&Settings);
    bool bSuccess = Proxy->DoExport();
    Proxy->RemoveFromRoot();

    return bSuccess;
}

BuildPatch(BaseVersion, TargetVersion, Platform, Template, BaseReleaseJson, OutputDir, OutPakPaths)
{
    // 1. 加载模板
    FExportPatchSettings Settings;
    LoadJsonToStruct(Template, Settings);

    // 2. 覆盖关键字段
    Settings.VersionId = TargetVersion;
    Settings.bByBaseVersion = true;
    Settings.BaseVersion.FilePath = BaseReleaseJson;
    Settings.SavePath = OutputDir;
    Settings.bStandaloneMode = false;
    Settings.bStorageNewRelease = true;   // 保存新 Release 供下次增量使用

    // 3. 设置目标平台
    Settings.PakTargetPlatforms 清空后添加 Platform 对应的枚举值

    // 4. 调用 HotPatcher Proxy
    UPatcherProxy* Proxy = NewObject<UPatcherProxy>();
    Proxy->AddToRoot();
    Proxy->Init(&Settings);
    bool bSuccess = Proxy->DoExport();
    Proxy->RemoveFromRoot();

    // 5. 收集产物路径
    if (bSuccess)
        扫描 OutputDir 下 *.pak 文件 → OutPakPaths

    return bSuccess;
}
```

### 为什么 MVP 用 Proxy 而不是 Commandlet 子进程

| 维度 | 直接调 Proxy | 套 HotPatcher Commandlet |
|------|-------------|------------------------|
| 简单性 | ✅ 一个函数调用 | ❌ 需要序列化 JSON + 启动子进程 + 等待 + 解析结果 |
| 错误处理 | ✅ DoExport() 返回 bool + 异常可 catch | ❌ 子进程退出码 + 日志解析 |
| 调试 | ✅ 可断点 | ❌ 另一个进程 |
| 产物收集 | ✅ 同进程直接访问 | ❌ 需要约定产物路径再扫描 |
| 稳定性 | ⚠️ Cook 失败可能影响 Commandlet 进程 | ✅ 子进程崩溃不影响调用者 |

MVP 阶段用直接调 Proxy，验证链路最快。后续如果 Cook 稳定性有问题，Adapter 内部可以切换为子进程模式，上层 Commandlet 代码不需要改。

---

## 6. MVP 实现步骤

### Step 1：创建插件骨架（30 分钟）
- 创建 `Plugins/G01HotUpdateTool/` 目录结构
- 写 `.uplugin`、`Build.cs`、空的模块入口
- 编译确认模块能加载

### Step 2：实现数据结构（1 小时）
- `FG01BuildTask`：BuildTask.json 对应的 USTRUCT
- `FG01VersionManifest`：Manifest 对应的 USTRUCT
- `FG01BuildReport`：Report 对应的 USTRUCT
- 所有结构体支持 JSON 序列化/反序列化
- 写一个 BuildTask_Example.json

### Step 3：实现 Adapter（2 小时）
- `FG01HotPatcherAdapter::ExportRelease()`
- `FG01HotPatcherAdapter::BuildPatch()`
- 内部：加载模板 JSON → 填充参数 → 创建 Proxy → DoExport
- 编译确认能调用 HotPatcher 的 Proxy

### Step 4：实现 Commandlet（2 小时）
- `UG01HotPatchCommandlet::Main()`
- 读取 BuildTask.json → 校验 → 调 Adapter → 计算 MD5 → 生成 Manifest + Report
- 追加 BuildHistory.json

### Step 5：端到端验证（1 小时）
- 准备 BuildTask.json（Android + Snapshot + 1.0.0 → 1.0.1）
- 命令行执行：
  ```
  UE-Cmd.exe TestHotpatch.uproject -run=G01HotPatch -config=BuildTask.json
  ```
- 验证产物：Release JSON、Patch Pak、VersionManifest、BuildReport、BuildHistory

### Step 6：补充第二种场景验证（30 分钟）
- 修改 Lua/资产
- 再准备一个 BuildTask（1.0.0 → 1.0.2 Snapshot）
- 验证增量正确性
- 验证 BuildHistory 追加正确

**总预估工作量：约 7 小时**

---

## 7. 可能遇到的问题

### 编译依赖

| 问题 | 说明 | 应对 |
|------|------|------|
| HotPatcherCore 的头文件路径 | Proxy 类在 `Public/CreatePatch/` 下，include 需要完整路径 | `#include "CreatePatch/ReleaseProxy.h"` |
| FExportPatchSettings 很大 | 属性非常多，但我们只改少数字段 | 先加载模板 JSON 填充所有默认值，再覆盖需要的字段 |
| ETargetPlatform 枚举 | HotPatcher 自定义的平台枚举，需要字符串 → 枚举转换 | 用 `THotPatcherTemplateHelper::GetEnumValueByName()` |

### 运行时问题

| 问题 | 说明 | 应对 |
|------|------|------|
| Cook 需要 Shader 编译 | 首次 Cook 很慢 | Release 模板里 `bNoShaderCompile=true` 可跳过 |
| Commandlet 进程内 Cook 内存占用 | Cook 大量资产可能内存不够 | MVP 阶段 TestHotpatch 项目很小不会有问题；G01 正式项目可在 Adapter 里切子进程模式 |
| 产物目录命名 | HotPatcher 自己的命名规则（{VERSION}/{PLATFORM}/）和我们规划的不完全一致 | Adapter 拿到产物后做一次目录整理（移动到我们规划的结构下） |
| GIsRunningCookCommandlet 标志 | HotPatcher Commandlet 会设置此标志，可能影响引擎退出（之前踩过的坑） | 在 G01HotPatchCommandlet 的 Main 末尾重置此标志 |
