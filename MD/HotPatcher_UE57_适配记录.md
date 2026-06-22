# HotPatcher UE 5.7 适配修改记录


---

## 修改清单

### 1. FlibReflectionHelper.cpp — FProperty 反射 API 适配

**原因**：UE 5.7 移除了 `FProperty::ExportTextItem` 和 `FProperty::ImportText` 旧版 API。

**改动**：
- `ExportTextItem` → `ExportTextItem_Direct`
- `ImportText` → `ImportText_Direct`

---

### 2. CmdHandler.cpp — FConfigFile API 适配

**原因**：UE 5.7 移除了 `FConfigFile::FindOrAddSection()`。

**改动**：用 `FindSection()` + `Add()` 替代。

---

### 3. MissionNotificationProxy.cpp — FGlobalTabmanager API 适配

**原因**：UE 5.7 移除了 `FGlobalTabmanager::InvokeTab()`。

**改动**：`InvokeTab` → `TryInvokeTab`

---

### 4. FCountServerlessWrapper.cpp — HTTP 超时 API 适配

**原因**：UE 5.7 移除了 `IHttpRequest::SetHttpTimeout()`。

**改动**：`SetHttpTimeout` → `SetTimeout`

---

### 5. MountListener.cpp — Pak 挂载委托适配

**原因**：UE 5.7 中 `FCoreDelegates::OnPakFileMounted2` 委托签名变更，参数从 `FPakFile&` 变为 `const IPakFile&`。

**改动**：更新 Lambda 参数类型，通过 `PakFile.PakGetPakFilename()` 获取文件名。

---

### 6. FCookShaderCollectionProxy.cpp — Shader 烹饪 API 适配

**原因**：UE 5.7 中 `FShaderLibraryCooker::InitForCooking` 参数签名变更，需要 `ICookArtifactReader*` 参数。

**改动**：
- 添加 `#include "CookArtifactReader.h"`
- `InitForCooking(bIsNative)` → `InitForCooking(bIsNative, nullptr)`

---

### 7. FlibShaderCodeLibraryHelper.cpp — Material 资源获取 API 适配

**原因**：UE 5.7 中 `Material->GetMaterialResource()` 参数类型变更。

**改动**：
- 添加 `#include "CookArtifactReader.h"`
- 显式 cast `FeatureLevel` 为 `ERHIFeatureLevel::Type`，增加 `ShaderPlatform` 临时变量

---

### 8. HotPatcherEditor.h / HotPatcherEditor.cpp — OnObjectSaved 委托适配

**原因**：UE 5.7 中 `FCoreUObjectDelegates::OnObjectPreSave` 委托签名变更，参数从 `UObject*` 变为 `(UObject*, FObjectPreSaveContext)`。

**改动**：
- 头文件：更新 `OnObjectSaved` 声明签名
- cpp：添加 `#include "UObject/ObjectSaveContext.h"`，更新委托绑定和实现

---

### 9. FlibAssetManageHelper.cpp — 软对象路径 & MetaData API 适配

**原因**：
1. UE 5.7 禁用了 `FSoftObjectPath` 的隐式构造
2. `UMetaData` 类名变更，`GetMetaData` 返回类型变更

**改动**：
- `FSoftObjectPath(AssetDetail.PackagePath.ToString())` 显式构造
- 添加 `#include "UObject/MetaData.h"`
- 使用 `#if WITH_METADATA` 条件编译包裹 MetaData 操作

---

### 10. FlibPatchParserHelper.cpp — ANY_PACKAGE 移除 & 宏参数修复

**原因**：
1. UE 5.7 移除了 `ANY_PACKAGE`
2. `UE_VERSION_OLDER_THAN(5,5)` 参数过多（仅支持 Major, Minor）

**改动**：
- `ANY_PACKAGE` → `EFindFirstObjectOptions::EnsureIfAmbiguous`
- `UE_VERSION_OLDER_THAN(5,5,0)` → `UE_VERSION_OLDER_THAN(5,5)`

---

### 11. HotPatcherPackageWriter.h / .cpp — UE 5.7 新增纯虚函数实现 ⚠️ 关键

**原因**：UE 5.7 在 `ICookedPackageWriter` 接口中新增了 6 个纯虚函数，`FHotPatcherPackageWriter` 必须实现。

**新增函数**：

| 函数 | 实现 | 参考来源 |
|------|------|---------|
| `CreateLinkerArchive` | 返回有效的 `FLargeMemoryWriter` | `BasePackageWriter.cpp` |
| `CreateLinkerExportsArchive` | 返回有效的 `FLargeMemoryWriter` | `BasePackageWriter.cpp` |
| `SetCooker` | 空实现 | `FLooseCookedPackageWriter` |
| `GetOplogAttachments` | 遍历并回调空 `FCbObject` | `FLooseCookedPackageWriter` |
| `GetBaseGameOplogAttachments` | 遍历并回调空 `FCbObject` | `FLooseCookedPackageWriter` |
| `GetCommitStatus` | 返回 `NotCommitted` | `FLooseCookedPackageWriter` |

> **⚠️ `CreateLinkerArchive` 和 `CreateLinkerExportsArchive` 如果返回空指针会导致打包时 `LinkerSave::AssignSaver` 断言崩溃。**

---

### 12. HotPatcherPackageWriter.cpp — TUniquePtr 析构编译修复

**原因**：`TUniquePtr<FAssetRegistryState>` 的析构需要完整类型定义。

**改动**：添加 `#include "AssetRegistry/AssetRegistryState.h"`

---

### 13. FlibHotPatcherCoreHelper.cpp — CookArtifactReader 编译修复

**原因**：`FArchiveCookData` 析构需要 `ICookArtifactReader` 完整类型。

**改动**：添加 `#include "CookArtifactReader.h"`

---

### 14. HotPatcherCommandlet.cpp — 退出前重置 cook flag（解决 UE 5.7 退出崩溃）⚠️ 关键

**原因**：UE 5.7 引擎退出时 `PurgeAllUObjectsOnExit` → `UWorldPartition::BeginDestroy` → `UnregisterCookPackageSubSplitterFactory` 触发断言崩溃。叫栈如下：

```
AppPreExit → StaticExit → PurgeAllUObjectsOnExit → IncrementalPurgeGarbage
→ UWorldPartition::BeginDestroy → UnregisterCookPackageSubSplitterFactory → ASSERT
```

根因：HotPatcher 在 `Main()` 开头设置了 `PRIVATE_GIsRunningCookCommandlet = true`，但返回前未重置。这导致引擎退出阶段销毁 `UWorldPartition` CDO 时，`IsRunningCookCommandlet()` 仍然返回 `true`，`BeginDestroy` 尝试从静态 `RegisteredCookPackageSubSplitterFactories` 中反注册该类，但该类从未被注册过（或 map 已被静态析构清空），触发 `check(Contains(Class))` 断言。

**改动**：在 `Main()` 返回前将 `PRIVATE_GIsRunningCookCommandlet` 重置为 `false`：

```cpp
#if WITH_UE5
    PRIVATE_GIsRunningCookCommandlet = false;
#endif
```

同时配合 `PatcherProxy` 清理和 `CollectGarbage` 强制 GC，双保险。

> **⚠️ 这是 UE 5.7 新增的断言。`UWorldPartition::BeginDestroy` 中的 `IsRunningCookCommandlet()` 检查是 UE 5.x 新增的防御性代码，但 `WorldCookPackageSplitter` 的注册/反注册机制在 HotPatcher 这类自定义 Commandlet 中从未配对调用过，导致退出时必然触发断言。**

---

## 已知问题

### IoStore 与 Patch pak 兼容性

UE 5.7 默认启用 `bUseIoStore=True`（`Engine/Config/BaseGame.ini`），打包出来的基础包是 `.ucas`/`.utoc` 格式。HotPatcher 打的传统 `.pak` patch 在 IoStore 基础包上**不会生效**：

- 运行时日志显示：`IoStore container "1.0.2_WindowsClient_001_P.utoc" not found`
- 引擎加载了 `.pak` 文件但找不到配套的 `.utoc`，实际内容未挂载

**解决方案**：
1. 在项目设置中关闭 `bUseIoStore=False`，重新打基础包（推荐小项目使用）
2. 或在 HotPatcher 的 patch 配置中启用 `ioStoreSettings.bIoStore=true`（需要 `bCookPatchAssets=false`）

---

## Android 热更验证记录

### Android 补丁 Pak 存放路径（已验证 ✅）

**正确路径**：

```
/storage/emulated/0/Android/data/{包名}/files/UnrealGame/{项目名}/{项目名}/Saved/Paks/
```

本项目实际路径：

```
/storage/emulated/0/Android/data/com.YourCompany.TestHotpatch/files/UnrealGame/TestHotpatch/TestHotpatch/Saved/Paks/
```

对应 UE 的 `FPaths::ProjectSavedDir() + "Paks/"`。

**验证结果**：
- 将 HotPatcher 生成的补丁 Pak（`1.0.1_Android_ASTC_001_P.pak`）通过 `adb push` 放入上述目录
- 引擎启动时自动扫描该目录，文件名以 `_P.pak` 结尾的被识别为补丁 Pak
- 补丁 Pak 以高于基础包的优先级挂载，场景中新增的蓝图 Actor 正确显示
- **无需编写任何加载代码**，引擎内置的 `FPakPlatformFile` 自动完成扫描和挂载

**测试命令**：

```cmd
adb push 补丁.pak /storage/emulated/0/Android/data/com.YourCompany.TestHotpatch/files/UnrealGame/TestHotpatch/TestHotpatch/Saved/Paks/
```

**注意事项**：
- OBB 所在目录（`/Android/obb/`）不会被引擎扫描额外 pak，不要放在那里
- `Content/Paks/` 目录也是引擎扫描路径之一，但实测 `Saved/Paks/` 生效
- 补丁 Pak 必须使用 Android 平台 Cook（HotPatcher 中 PakTargetPlatforms 选 Android），不能用 Windows Cook 的 Pak

---

## 注意事项

1. 所有修改仅针对 UE 5.7，去掉了不必要的条件编译（如 `ENGINE_MAJOR_VERSION > 4` 等）
2. 除 `HotPatcherPackageWriter` 的 6 个纯虚函数外，其余均为 API 等价替换，不影响原有功能
3. `HotPatcherPackageWriter` 的新增实现参考了 UE 5.7 引擎自带的 `BasePackageWriter.cpp` 和 `FLooseCookedPackageWriter.cpp`
