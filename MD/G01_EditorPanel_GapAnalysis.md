# G01 热更新 Editor 面板 — 适配性分析与差距报告

> 日期：2026-06-24
> 状态：分析阶段

---

## 1. 当前 Editor 面板已有字段和功能

**当前没有 Editor 面板。** G01HotUpdateTool 插件目前只有：

| 已有 | 状态 |
|------|------|
| G01HotPatchCommandlet | ✅ 可用，支持 ExportRelease / BuildPatch |
| G01HotPatcherAdapter | ✅ 可用，封装 HotPatcher Proxy 调用 |
| FG01BuildTask | ✅ 可用，BuildTask.json 读写+校验 |
| FG01VersionManifest | ✅ 可用，Manifest JSON 生成 |
| FG01BuildReport / BuildHistory | ✅ 可用，Report+History JSON 生成 |
| Slate 面板 | ❌ 不存在 |
| 菜单/Tab 注册 | ❌ 不存在 |
| 版本状态展示 | ❌ 不存在 |
| 确认摘要 / 校验 UI | ❌ 不存在 |

---

## 2. 当前面板是否只是 Commandlet 参数输入

不是——因为面板还不存在。但如果直接做一个"填参数 → 调 Commandlet"的面板，那确实只是参数输入，不满足需求。

用户真正需要的面板应该是：
- **版本状态看板**（一眼看到当前有哪些 Release/Patch、它们的关系）
- **业务动作入口**（生成 Release / Snapshot / Merged，而不是 Import Config）
- **确认摘要**（点击前看到即将做什么、影响什么）
- **结果展示**（构建完成后看到产物、MD5、大小）

---

## 3. 与正式"非程序出包工具"的差距

### 3.1 数据结构差距

| 需求 | 当前 BuildHistory | 差距 |
|------|-------------------|------|
| 区分 Release 和 Patch 记录 | ✅ PatchType 字段可区分（"Release" / "Snapshot"） | 无 |
| Release 版本号、时间、路径、平台 | ✅ 有 | 无 |
| Patch 的 baseVersion / targetVersion | ✅ 有 | 无 |
| Pak 路径、Size、MD5 | ⚠️ 在 BuildReport 里有，BuildHistory 只有 ReportPath | 需要在 History 里加 Size/MD5 摘要，或面板读 Report |
| 当前最新资源版本 | ❌ 没有字段 | 需要从 History 推算或增加 latestVersion 字段 |
| Patch 过期/可替代标记 | ❌ 没有 | 需要 Merged Patch 支持后增加 |

| 需求 | 当前 VersionManifest | 差距 |
|------|---------------------|------|
| patchType = Merged | ❌ 只有 Snapshot/Incremental | 需要增加 Merged |
| containsVersions | ❌ 不存在 | 需要新增 |
| replacesPatches / obsoletePatches | ❌ 不存在 | 需要新增 |
| mountOrder | ❌ 不存在 | 需要新增 |

### 3.2 UI 差距

| 需求 | 差距 |
|------|------|
| 版本关系树/列表 | 完全没有 |
| 业务动作按钮（生成 Release / Snapshot / Merged） | 完全没有 |
| 确认摘要弹窗 | 完全没有 |
| 构建进度显示 | 完全没有 |
| 构建结果展示 | 完全没有 |
| 基准版本下拉选择 | 完全没有 |
| 版本号自动推荐 | 完全没有 |

---

## 4. 需要新增的 UI 区块

### 面板整体布局建议

```
┌──────────────────────────────────────────────────┐
│  G01 HotUpdate Tool                    [Refresh] │
├──────────────────────────────────────────────────┤
│                                                  │
│  ┌─── 版本状态区 ───────────────────────────┐    │
│  │                                          │    │
│  │  Releases:                               │    │
│  │  ┌─────────────────────────────────────┐ │    │
│  │  │ 1.0.0  Android  2026-06-24  ✅     │ │    │
│  │  └─────────────────────────────────────┘ │    │
│  │                                          │    │
│  │  Patches:                                │    │
│  │  ┌─────────────────────────────────────┐ │    │
│  │  │ 1.0.1  Snapshot  base=1.0.0  15MB  │ │    │
│  │  │ 1.0.2  Snapshot  base=1.0.0  2MB   │ │    │
│  │  └─────────────────────────────────────┘ │    │
│  │                                          │    │
│  │  Latest Version: 1.0.2                   │    │
│  └──────────────────────────────────────────┘    │
│                                                  │
│  ┌─── 操作区 ──────────────────────────────┐    │
│  │                                          │    │
│  │  Action:    [ExportRelease ▼]            │    │
│  │  Platform:  [Android ▼]                  │    │
│  │  Base Ver:  [1.0.0 ▼]  (从已有Release选) │    │
│  │  Target:    [1.0.3   ]  (自动推荐下一个)  │    │
│  │  Type:      [Snapshot ▼]                 │    │
│  │                                          │    │
│  │  [Generate]                              │    │
│  └──────────────────────────────────────────┘    │
│                                                  │
│  ┌─── 确认摘要区（点 Generate 后显示）────┐     │
│  │                                          │    │
│  │  即将生成：                               │    │
│  │  平台：Android                           │    │
│  │  类型：Snapshot Patch                    │    │
│  │  基准：1.0.0                             │    │
│  │  目标：1.0.3                             │    │
│  │  输出：Saved/HotPatcher/Patches/1.0.3/  │    │
│  │                                          │    │
│  │  ✅ Base Release 1.0.0 exists           │    │
│  │  ✅ Target 1.0.3 > Base 1.0.0           │    │
│  │  ✅ IoStore disabled (Pak-only)          │    │
│  │  ✅ No output conflict                  │    │
│  │                                          │    │
│  │  [Confirm & Build]    [Cancel]           │    │
│  └──────────────────────────────────────────┘    │
│                                                  │
│  ┌─── 构建结果区（构建完成后显示）─────────┐    │
│  │                                          │    │
│  │  ✅ BUILD COMPLETE                       │    │
│  │  Pak: 1.0.3_Android_001_P.pak           │    │
│  │  Size: 15,728,640 bytes                  │    │
│  │  MD5: a1b2c3d4...                        │    │
│  │  Duration: 42.5s                         │    │
│  │                                          │    │
│  │  [Open Output Dir] [Push to Device]      │    │
│  └──────────────────────────────────────────┘    │
│                                                  │
└──────────────────────────────────────────────────┘
```

### 区块职责

| 区块 | 功能 | 数据来源 |
|------|------|---------|
| 版本状态区 | 展示已有 Release/Patch，版本关系 | BuildHistory.json + BuildReport.json |
| 操作区 | 选择动作、平台、版本号 | 用户输入 + 从 History 推算 |
| 确认摘要区 | 校验+摘要展示，用户二次确认 | BuildTask 校验逻辑 |
| 构建结果区 | 展示产物信息 | BuildReport.json + VersionManifest.json |

---

## 5. 需要新增的 Manifest 字段

```cpp
// 当前 FG01VersionManifest 需要扩展的字段

UPROPERTY()
FString PatchType;              // 已有。值域扩展为："Snapshot" / "Incremental" / "Merged"

UPROPERTY()
TArray<FString> ContainsVersions;   // 新增。Merged 时记录包含哪些版本
                                     // 例如 ["1.0.4", "1.0.5", "1.0.6"]

UPROPERTY()
TArray<FString> ReplacesPatches;    // 新增。该补丁可替代哪些旧补丁
                                     // 客户端安装后可删除这些旧 pak

UPROPERTY()
int32 MountOrder = 100;            // 新增。客户端挂载优先级
                                    // 数字越大优先级越高
```

ExportRelease 和 Snapshot 场景下 ContainsVersions 和 ReplacesPatches 为空数组，不影响现有逻辑。

---

## 6. 需要新增的 BuildHistory 字段

```cpp
// 当前 FG01BuildHistoryEntry 需要扩展的字段

UPROPERTY()
TArray<FString> ContainsVersions;   // 新增。同 Manifest

UPROPERTY()
int64 TotalPakSize = 0;            // 新增。总 Pak 大小（不用打开 Report 就能展示）

UPROPERTY()
FString PakMD5;                    // 新增。主 Pak 的 MD5（便于面板展示）
```

同时建议在 BuildHistory 根级增加：

```cpp
// FG01BuildHistory 根级字段
UPROPERTY()
FString LatestReleaseVersion;      // 最新的 Release 版本号

UPROPERTY()
FString LatestPatchVersion;        // 最新的 Patch 版本号
```

面板打开时直接读这两个字段就知道当前版本状态，不用遍历 Entries。

---

## 7. Merged Patch 最小数据结构

### BuildTask.json 扩展

```json
{
    "taskType": "BuildPatch",
    "platform": "Android",
    "patchType": "Merged",
    "baseVersion": "1.0.3",
    "targetVersion": "1.0.6",
    "containsVersions": ["1.0.4", "1.0.5", "1.0.6"],
    "releaseConfigTemplate": "ReleaseTest.json",
    "patchConfigTemplate": "PatchTest.json",
    "outputDir": "Saved/HotPatcher"
}
```

### Merged Patch 的构建逻辑

Merged Patch 的构建方式和 Snapshot 完全一样——都是"当前项目内容 vs baseVersion 的 Release"生成差异。区别只在于 Manifest 里记录的元信息不同：

```
Snapshot 1.0.6 (base=1.0.0):
  含义：从基础包 1.0.0 到 1.0.6 的全量差异
  containsVersions: []
  replacesPatches: []

Merged 1.0.6 (base=1.0.3):
  含义：从中间 Release 1.0.3 到 1.0.6 的合并差异
  containsVersions: ["1.0.4", "1.0.5", "1.0.6"]
  replacesPatches: ["1.0.4", "1.0.5", "1.0.6"]
```

构建时 Adapter 调用逻辑完全相同（读 Release_1.0.3 → 对比当前内容 → 生成 pak），只是 Manifest 写入额外字段。不需要新的 Adapter 接口。

### 客户端安装 Merged Patch 的行为

1. 下载 Merged pak
2. 删除 Saved/Paks 下所有 containsVersions 对应的旧 pak
3. 安装 Merged pak
4. 版本号记录为 targetVersion (1.0.6)

---

## 8. MVP 阶段建议先实现的 UI

### Phase 2a（最小可用面板）

| UI | 必要性 | 说明 |
|----|--------|------|
| 版本列表（只读展示） | ✅ 必须 | 读 BuildHistory.json 展示表格 |
| Action 下拉框 | ✅ 必须 | ExportRelease / BuildPatch |
| Platform 下拉框 | ✅ 必须 | MVP 只有 Android |
| Base Version 下拉框 | ✅ 必须 | 从已有 Release 列表中选 |
| Target Version 输入框 | ✅ 必须 | 自动推荐下一个版本号 |
| Generate 按钮 | ✅ 必须 | 生成 BuildTask.json → 启动 Commandlet |
| 构建结果展示 | ✅ 必须 | 读 BuildReport 展示成功/失败+产物信息 |

### Phase 2b（确认+校验）

| UI | 必要性 | 说明 |
|----|--------|------|
| 确认摘要弹窗 | 建议 | 点 Generate 后先展示摘要让用户确认 |
| 基础校验提示 | 建议 | Release 是否存在、版本号是否合法 |
| 构建进度 | 建议 | 监听 Commandlet 进程输出 |

### Phase 2c（体验优化）

| UI | 必要性 | 说明 |
|----|--------|------|
| PatchType 选择（Snapshot/Merged） | 后续 | Merged 预留 |
| Open Output Dir 按钮 | 后续 | FPlatformProcess::ExploreFolder |
| Push to Device 按钮 | 后续 | 集成 push_patch.bat |
| 版本号自动递增 | 后续 | 读 History 的 latestPatchVersion |

---

## 9. 哪些能力后续再做

| 能力 | 阶段 | 说明 |
|------|------|------|
| Incremental Patch | Phase 3 | 结构已预留，需要 Adapter 支持多 Release 基准 |
| Merged Patch | Phase 3 | 构建逻辑和 Snapshot 相同，只是 Manifest 多几个字段 |
| iOS / Windows 平台 | Phase 3 | Adapter.GetTargetPlatformEnum 扩展 |
| IoStore (.utoc/.ucas) | 不确定 | 需要先确认项目是否会启用 IoStore |
| CDN 上传 | Phase 4 | 需要服务端对接 |
| 服务器 Manifest API | Phase 4 | VersionManifest 已经是数据基础 |
| CI/CD 集成 | Phase 3 | Commandlet 已经支持命令行调用 |
| 构建进度实时显示 | Phase 2b | 需要监听子进程 stdout |
| 版本关系可视化（树/图） | Phase 4 | 当版本多了之后有价值 |
| 多语言/多渠道产物管理 | 不在范围 | 需求明确后再设计 |

---

## 总结

当前 Commandlet 层的核心能力已经具备，数据结构基本够用（Manifest/Report/History），主要差距在：

1. **没有面板** — 需要从零搭建 Slate UI
2. **Manifest 缺少 Merged 字段** — containsVersions / replacesPatches / mountOrder，但代码改动很小
3. **History 缺少摘要字段** — TotalPakSize / PakMD5 / LatestVersion，方便面板直接读取
4. **没有确认摘要和校验 UI** — 需要在 Generate 和实际构建之间加一层人工确认

建议 Phase 2a 先做最小可用面板（版本列表+操作区+结果展示），不需要 Merged Patch 和进度显示。
