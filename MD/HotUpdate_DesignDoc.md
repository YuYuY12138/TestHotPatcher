# Android 热更新系统设计文档

> 项目：TestHotpatch  
> 引擎：UE 5.7 | 脚本：UnLua | 补丁工具：HotPatcher  
> 目标平台：Android  
> 日期：2026-06-16

---

## 一、系统概述

### 1.1 目标

在不重新上架应用商店的前提下，对已发布的 Android 游戏包进行内容和逻辑更新，包括：

- **Lua 脚本热更**（UnLua）—— 修复逻辑 Bug、调整数值、新增玩法
- **资源热更**（Pak）—— 替换/新增美术资源、UI、关卡、数据表
- **Shader 热更** —— 更新材质的编译产物（ShaderBytecode）

### 1.2 不可热更的内容

| 内容 | 原因 |
|------|------|
| C++ 代码（.so） | 需要重新编译原生二进制，涉及 ABI 兼容性 |
| 引擎核心模块 | 与 .so 绑定，无法独立替换 |
| AndroidManifest / Gradle 配置 | 属于 APK 安装包结构，需重新安装 |
| 首包内已加密且无法覆盖的 Pak | 需要引擎层面支持优先级覆盖 |

### 1.3 技术选型依据

```
┌──────────────────────────────────────────────────────┐
│  HotPatcher (imzlp)                                  │
│  ✓ Pak 粒度差异对比与打包                             │
│  ✓ 运行时 Pak 挂载 / 卸载 / 版本管理                  │
│  ✓ Shader Library 热加载                              │
│  ✓ Asset Registry 增量合并                            │
│  ✓ 支持 Chunk 分包                                   │
├──────────────────────────────────────────────────────┤
│  DownloadToolkit (ue4-dtkit)                         │
│  ✓ HTTP 分片下载 + 断点续传                           │
│  ✓ 边下边算 MD5 校验                                  │
│  ✓ 蓝图可调用，暂停/恢复/取消                         │
├──────────────────────────────────────────────────────┤
│  UnLua                                               │
│  ✓ Lua 替代蓝图，脚本文件可通过 Pak 热替换            │
│  ✓ 支持 require 缓存清理与模块重载                    │
├──────────────────────────────────────────────────────┤
│  Pak 模式（bUseIoStore=False）                       │
│  ✓ 传统 .pak 格式，HotPatcher 完整支持               │
│  ✓ 运行时可动态挂载，支持优先级覆盖                    │
└──────────────────────────────────────────────────────┘
```

---

## 二、系统架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        版本管理服务器                             │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────┐  │
│  │ 版本数据库   │  │ CDN / OSS    │  │  管理后台（Web）        │  │
│  │             │  │ (Pak 文件存储)│  │  · 版本发布             │  │
│  │ · 版本号    │  │              │  │  · 灰度策略             │  │
│  │ · 变更清单  │  │              │  │  · 强制更新 / 可选更新   │  │
│  │ · 下载地址  │  │              │  │  · 回滚操作             │  │
│  │ · 校验信息  │  │              │  │                        │  │
│  └──────┬──────┘  └──────┬───────┘  └────────────────────────┘  │
│         │                │                                       │
└─────────┼────────────────┼───────────────────────────────────────┘
          │  版本检查 API    │  CDN 下载
          ▼                ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Android 客户端                              │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                   热更新管理器 (UpdateManager)             │   │
│  │                                                          │   │
│  │  ┌──────────┐  ┌───────────┐  ┌────────┐  ┌──────────┐  │   │
│  │  │ 版本检查  │→│  下载管理  │→│ 校验   │→│  安装应用  │  │   │
│  │  │ Checker  │  │ Downloader│  │Verifier│  │ Installer │  │   │
│  │  └──────────┘  └───────────┘  └────────┘  └──────────┘  │   │
│  └──────────────────────────────────────────────────────────┘   │
│           │                                      │               │
│           ▼                                      ▼               │
│  ┌────────────────┐                    ┌─────────────────────┐  │
│  │DownloadToolkit │                    │ HotPatcher Runtime  │  │
│  │· HTTP 分片下载  │                    │ · MountPak          │  │
│  │· 断点续传      │                    │ · LoadShaderLibrary │  │
│  │· MD5 校验      │                    │ · LoadAssetRegistry │  │
│  └────────────────┘                    │ · MountListener     │  │
│                                        └─────────────────────┘  │
│           │                                      │               │
│           ▼                                      ▼               │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    UE Pak 文件系统                          │  │
│  │  Pak 0 (基础包)    Pak 1 (补丁 v1.0.1)   Pak 2 (v1.0.2)   │  │
│  │  Priority: 0       Priority: 1            Priority: 2      │  │
│  │                    覆盖基础包同路径资源     覆盖前两者        │  │
│  └────────────────────────────────────────────────────────────┘  │
│                              │                                   │
│                              ▼                                   │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    UnLua Runtime                            │  │
│  │  package.loaded 缓存清理 → require 重载 → Lua 逻辑生效      │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 模块职责

| 模块 | 职责 | 实现位置 |
|------|------|---------|
| **UpdateManager** | 热更新总调度，驱动整个生命周期 | 项目层（待开发） |
| **VersionChecker** | 向服务器请求版本清单，比对本地版本 | 项目层（待开发） |
| **DownloadManager** | 调用 DownloadToolkit 执行下载任务队列 | 项目层 + DownloadToolkit |
| **PakInstaller** | 调用 HotPatcher Runtime 挂载 Pak、加载 Shader/AssetRegistry | 项目层 + HotPatcherRuntime |
| **LuaReloader** | 清理 UnLua 缓存并触发 Lua 模块重载 | 项目层 + UnLua |
| **MountListener** | 监听 Pak 挂载/卸载事件，通知上层 | HotPatcherRuntime（已有） |
| **FlibPakHelper** | Pak 挂载/卸载/列表/校验的底层操作 | HotPatcherRuntime（已有） |
| **UDownloadProxy** | 单文件 HTTP 下载 + MD5 计算 | DownloadToolkit（已有） |

---

## 三、版本管理

### 3.1 版本号规范

```
主版本.次版本.补丁号
  │       │      │
  │       │      └── 热更新迭代（每次出补丁 +1）
  │       └──────── 功能版本（需要新基础包时 +1）
  └──────────────── 大版本（重大更新，需重新上架时 +1）

示例：
  1.0.0   ← 首包（基础包）
  1.0.1   ← 第 1 次热更
  1.0.2   ← 第 2 次热更
  1.1.0   ← 新功能上线，需要新基础包
  1.1.1   ← 基于 1.1.0 的第 1 次热更
```

### 3.2 版本清单（VersionManifest）

服务器返回的版本检查响应：

```json
{
  "appVersion": "1.0.0",
  "latestPatchVersion": "1.0.3",
  "minSupportedPatchVersion": "1.0.0",
  "forceUpdate": false,
  "patches": [
    {
      "version": "1.0.1",
      "baseVersion": "1.0.0",
      "pakUrl": "https://cdn.example.com/patches/1.0.1/patch_1.0.1.pak",
      "pakSize": 15728640,
      "md5": "a1b2c3d4e5f6...",
      "sha256": "abcdef123456...",
      "releaseNote": "修复登录闪退",
      "required": true
    },
    {
      "version": "1.0.2",
      "baseVersion": "1.0.0",
      "pakUrl": "https://cdn.example.com/patches/1.0.2/patch_1.0.2.pak",
      "pakSize": 8388608,
      "md5": "f6e5d4c3b2a1...",
      "sha256": "654321fedcba...",
      "releaseNote": "新增春节活动",
      "required": false
    }
  ]
}
```

### 3.3 版本策略

| 策略 | 说明 |
|------|------|
| **全量补丁** | 每个补丁都基于基础包生成，包含该版本所有差异。客户端只需下载最新的一个 Pak。体积较大但逻辑简单，推荐中小项目使用 |
| **增量补丁** | 每个补丁只包含相对上一版本的差异。客户端需按顺序下载并应用所有缺失版本。体积小但需要严格的版本链 |
| **混合策略（推荐）** | 落后 ≤2 个版本时走增量补丁；落后 >2 个版本时下发一个合并的全量补丁。平衡下载量和版本管理复杂度 |

### 3.4 本地版本持久化

```
Android 内部存储
└── UE4Game/{ProjectName}/
    └── Saved/
        ├── HotUpdate/
        │   ├── version.json          ← 当前版本信息
        │   ├── pending/              ← 下载中的临时文件
        │   ├── verified/             ← 校验通过待安装的 Pak
        │   └── installed/            ← 已挂载的 Pak 文件
        │       ├── patch_1.0.1.pak
        │       ├── patch_1.0.2.pak
        │       └── ...
        └── Extension/
            └── Versions/             ← HotPatcher 版本描述文件
```

`version.json` 示例：

```json
{
  "baseVersion": "1.0.0",
  "currentVersion": "1.0.2",
  "installedPatches": [
    {
      "version": "1.0.1",
      "pakFile": "patch_1.0.1.pak",
      "md5": "a1b2c3d4e5f6...",
      "installTime": "2026-06-15T10:30:00Z",
      "mountOrder": 1
    },
    {
      "version": "1.0.2",
      "pakFile": "patch_1.0.2.pak",
      "md5": "f6e5d4c3b2a1...",
      "installTime": "2026-06-16T08:00:00Z",
      "mountOrder": 2
    }
  ]
}
```

---

## 四、热更新生命周期

### 4.1 完整流程

```
              ┌─────────────────────────────────────────────────┐
              │                   游戏启动                       │
              └─────────────────────┬───────────────────────────┘
                                    │
                                    ▼
              ┌─────────────────────────────────────────────────┐
              │  Phase 0: 加载本地已安装的补丁 Pak                │
              │  · 扫描 Saved/HotUpdate/installed/ 目录          │
              │  · 按 mountOrder 升序挂载所有 .pak               │
              │  · 加载对应的 ShaderLibrary 和 AssetRegistry     │
              │  · 触发 UnLua Lua 文件重载                       │
              └─────────────────────┬───────────────────────────┘
                                    │
                                    ▼
              ┌─────────────────────────────────────────────────┐
              │  Phase 1: 版本检查                               │
              │  · HTTP GET → 版本管理服务器                      │
              │  · 比对 localVersion vs latestPatchVersion       │
              │  · 判定：无更新 / 可选更新 / 强制更新              │
              └─────────────────────┬───────────────────────────┘
                            ┌───────┼───────┐
                            ▼       ▼       ▼
                        无更新   可选更新   强制更新
                          │       │         │
                          │       ▼         ▼
                          │   ┌───────────────────┐
                          │   │ 显示更新提示 UI    │
                          │   │ · 更新内容说明     │
                          │   │ · 下载大小预估     │
                          │   │ · "立即更新/稍后"  │
                          │   └────────┬──────────┘
                          │            │ 用户确认
                          │            ▼
                          │   ┌─────────────────────────────────┐
                          │   │  Phase 2: 下载                   │
                          │   │  · 创建 UDownloadProxy 实例      │
                          │   │  · HEAD 获取文件大小              │
                          │   │  · GET 分片下载（支持断点续传）    │
                          │   │  · 显示进度条 UI                  │
                          │   │  · 边下载边计算 MD5               │
                          │   └───────────────┬─────────────────┘
                          │                   │
                          │                   ▼
                          │   ┌─────────────────────────────────┐
                          │   │  Phase 3: 校验                   │
                          │   │  · 比对 MD5 / SHA256             │
                          │   │  · 校验失败 → 删除文件 → 重试    │
                          │   │  · 校验成功 → 移至 verified/     │
                          │   └───────────────┬─────────────────┘
                          │                   │
                          │                   ▼
                          │   ┌─────────────────────────────────┐
                          │   │  Phase 4: 安装（Pak 挂载）       │
                          │   │  · 移动 Pak 到 installed/        │
                          │   │  · UFlibPakHelper::MountPak()    │
                          │   │  · 加载 ShaderLibrary            │
                          │   │  · 加载增量 AssetRegistry        │
                          │   │  · 更新 version.json             │
                          │   └───────────────┬─────────────────┘
                          │                   │
                          │                   ▼
                          │   ┌─────────────────────────────────┐
                          │   │  Phase 5: Lua 热重载（UnLua）    │
                          │   │  · 清理 package.loaded 缓存     │
                          │   │  · 重新 require 变更的模块       │
                          │   │  · 重新绑定 UObject              │
                          │   └───────────────┬─────────────────┘
                          │                   │
                          ▼                   ▼
              ┌─────────────────────────────────────────────────┐
              │  Phase 6: 进入游戏主逻辑                          │
              └─────────────────────────────────────────────────┘
```

### 4.2 各阶段详细说明

#### Phase 0 — 启动加载已安装补丁

**时机**：引擎初始化完成后、进入主菜单之前。

```
执行步骤：
1. 读取 version.json，获取已安装补丁列表
2. 按 mountOrder 从小到大遍历：
   a. FPlatformFileManager::Get().GetPlatformFile().MountPak(PakPath, MountOrder)
      或 UFlibPakHelper::MountPak(PakPath, MountOrder)
   b. UFlibPakHelper::LoadShaderbytecode(PakPath)     — 如果包含 Shader
   c. UFlibPakHelper::LoadAssetRegistry(PakPath)       — 合并增量 AssetRegistry
3. 如果 Pak 中包含 Lua 脚本（Script/ 目录），触发 UnLua 重载

关键点：
  - MountOrder 越大优先级越高，同路径资源以高优先级 Pak 为准
  - 必须在任何游戏资源加载之前完成挂载
  - 基础包 Pak 的 Order 默认为 0，补丁从 1 开始递增
```

#### Phase 1 — 版本检查

```
输入：本地 currentVersion、设备信息（OS版本、渠道号、设备ID）
输出：更新决策（无更新 / 可选 / 强制）+ 待下载补丁列表

版本检查 API 请求：
  GET /api/v1/check-update
    ?appVersion=1.0.0
    &patchVersion=1.0.1
    &platform=android
    &channel=google_play
    &deviceId=xxxx

服务器逻辑：
  if patchVersion >= latestPatchVersion → 无更新
  if patchVersion < minSupportedPatchVersion → 强制更新（需要新基础包）
  else → 返回增量/全量补丁列表
```

#### Phase 2 — 下载

```
使用 DownloadToolkit 的 UDownloadProxy：

  UDownloadProxy* Proxy = NewObject<UDownloadProxy>();
  Proxy->OnDownloadComplete.AddDynamic(this, &OnPatchDownloaded);
  
  FDownloadFile File;
  File.URL = PatchInfo.PakUrl;
  File.SavePath = FPaths::Combine(PendingDir, PatchInfo.PakFile);
  
  Proxy->RequestDownload(File.URL, File.SavePath, true, 20 * 1024 * 1024);
  //                                               bSlice  SliceSize=20MB

特性：
  · 分片下载：每片 20MB，避免单次请求超时
  · 断点续传：暂停后 Resume() 从已下载位置继续
  · 边下边算 MD5：下载完成后立即有校验值，无需二次读取
  · 下载速度计算：通过 Tick 驱动的 DeltaTime 计算 KB/s
```

#### Phase 3 — 校验

```
校验策略（三重验证）：

1. 文件大小校验
   if FileSize != PatchInfo.PakSize → FAIL

2. MD5 校验（DownloadToolkit 下载时已计算）
   if DownloadProxy.HashCheck() != PatchInfo.MD5 → FAIL

3. Pak 完整性校验（可选，防篡改）
   if UFlibPakHelper::GetPakFileList(PakPath) 解析失败 → FAIL

校验失败处理：
  · 自动重试下载（最多 3 次）
  · 3 次仍失败 → 提示用户检查网络，提供"重试"按钮
  · 记录失败日志上报服务器
```

#### Phase 4 — 安装（Pak 挂载）

```
安装步骤：
1. 将 Pak 从 pending/ 移动到 installed/
   IFileManager::Get().Move(DestPath, SrcPath);

2. 挂载 Pak
   bool bSuccess = UFlibPakHelper::MountPak(PakPath, MountOrder);

3. 加载 Shader Library（如果补丁包含 Shader）
   UFlibPakHelper::LoadShaderbytecode(ShaderLibName, PakMountDir);

4. 加载增量 Asset Registry
   UFlibPakHelper::LoadAssetRegistry(RegistryName, PakMountDir);

5. 更新 version.json
   写入新补丁记录，更新 currentVersion

注意事项：
  - 挂载顺序必须严格按版本号递增，否则资源覆盖关系错乱
  - 如果挂载失败（返回 false），回滚：删除 Pak 文件，恢复 version.json
  - Android 上文件路径使用 FPaths::ProjectSavedDir()，实际指向
    /storage/emulated/0/UE4Game/{ProjectName}/Saved/
```

#### Phase 5 — Lua 热重载（UnLua）

```
UnLua 脚本热更的核心问题：Lua VM 的 package.loaded 缓存

场景：补丁 Pak 包含 Script/UI/MainMenu.lua（覆盖基础包中的同名文件）

重载步骤：
1. 收集变更的 Lua 文件列表（从补丁 Pak 的 FileList 中过滤 .lua 文件）
2. 遍历变更列表：
   a. 清除 package.loaded 中对应模块的缓存
      package.loaded["UI.MainMenu"] = nil
   b. 如果该模块有绑定的 UObject，解绑旧 Lua 表
   c. 重新 require("UI.MainMenu")
   d. 重新绑定 UObject

伪代码（C++ 侧调用 UnLua API）：
  lua_State* L = UnLua::GetState();
  
  for (const FString& LuaModule : ChangedLuaModules)
  {
      // 清除缓存
      lua_getglobal(L, "package");
      lua_getfield(L, -1, "loaded");
      lua_pushnil(L);
      lua_setfield(L, -2, TCHAR_TO_UTF8(*LuaModule));
      lua_pop(L, 2);
      
      // 重新加载
      FUnLuaDelegates::OnLuaFileLoaded.Broadcast(LuaModule);
  }

限制：
  - 已实例化的 Actor/Widget 的 Lua 绑定不会自动更新
  - 如果热更涉及 UI 或 Actor 逻辑，建议在挂载后重新加载关卡
  - 全局状态（全局变量、单例表）不会被清除，需要手动处理
```

---

## 五、Pak 覆盖机制

### 5.1 优先级规则

UE 的 Pak 文件系统基于 **挂载优先级（Mount Order）** 实现资源覆盖：

```
请求加载资源: /Game/UI/MainMenu.uasset

搜索顺序（优先级从高到低）：
  Pak 2 (patch_1.0.2.pak, Order=2)  ← 找到 → 使用此版本 ✓
  Pak 1 (patch_1.0.1.pak, Order=1)  ← 跳过
  Pak 0 (基础包.pak, Order=0)       ← 跳过
```

### 5.2 挂载点（Mount Point）

```
默认挂载点: ../../../{ProjectName}/
  → 映射到引擎的虚拟文件系统根目录

补丁 Pak 的内容路径应与基础包一致：
  基础包中：  {ProjectName}/Content/UI/MainMenu.uasset
  补丁 Pak 中：{ProjectName}/Content/UI/MainMenu.uasset
  
  路径完全一致 → 高优先级 Pak 中的版本覆盖低优先级版本
```

### 5.3 资源引用注意事项

```
场景：补丁中更新了 DataTable_Items.uasset，但它引用了 T_ItemIcon_001.uasset

情况 A：T_ItemIcon_001 未变更
  → 引擎从基础包加载图标，从补丁包加载 DataTable ✓

情况 B：T_ItemIcon_001 也变更了但未放入补丁
  → 引擎从基础包加载旧图标 → 可能导致不一致 ✗

原则：HotPatcher 在生成补丁时默认会分析依赖链，
      将所有变更资源及其被影响的依赖一并打入补丁包。
      但外部文件（非 Cook 资源）需要手动配置。
```

---

## 六、UnLua 脚本热更详细设计

### 6.1 Lua 文件在 Pak 中的组织

```
补丁 Pak 内部结构：
  {ProjectName}/
  ├── Content/
  │   ├── Script/                    ← UnLua 脚本目录
  │   │   ├── UI/
  │   │   │   ├── MainMenu.lua
  │   │   │   └── HUD.lua
  │   │   ├── Logic/
  │   │   │   ├── BattleSystem.lua
  │   │   │   └── InventoryManager.lua
  │   │   └── Config/
  │   │       └── GameConfig.lua
  │   ├── BP/                        ← 蓝图资源（如有变更）
  │   └── UI/                        ← UI 资源（如有变更）
  └── AssetRegistry.bin              ← 增量资源注册表
```

### 6.2 Lua 热重载的三种策略

| 策略 | 适用场景 | 实现复杂度 | 体验 |
|------|---------|-----------|------|
| **场景重载** | 更新内容较多 | 低 | 需要过场/Loading |
| **模块重载** | 仅修改少量 Lua 文件 | 中 | 无缝，但有限制 |
| **全量重启 Lua VM** | Lua 架构变更 | 低 | 需要重新初始化所有 Lua 状态 |

#### 策略 A — 场景重载（推荐用于大版本热更）

```
1. 挂载补丁 Pak
2. 显示 Loading 界面
3. UGameplayStatics::OpenLevel(World, CurrentLevel)
   → 引擎卸载当前关卡的所有 Actor
   → UnLua 自动解绑所有 Lua 表
   → 重新加载关卡时，从高优先级 Pak 读取新 Lua 文件
   → UnLua 重新绑定
4. 隐藏 Loading 界面
```

#### 策略 B — 模块级重载（推荐用于小修复）

```lua
-- HotReload.lua：运行时 Lua 模块重载器

local HotReload = {}

function HotReload.ReloadModule(moduleName)
    -- 1. 解除旧模块的 UObject 绑定
    local oldModule = package.loaded[moduleName]
    if oldModule and oldModule.__unbind then
        oldModule:__unbind()
    end
    
    -- 2. 清除缓存
    package.loaded[moduleName] = nil
    
    -- 3. 清除 require 搜索缓存（UE 文件系统可能有缓存）
    if package.searchpath then
        -- 强制下次 require 从文件系统重新读取
    end
    
    -- 4. 重新加载
    local success, newModule = pcall(require, moduleName)
    if not success then
        -- 加载失败，恢复旧模块
        package.loaded[moduleName] = oldModule
        UE.UKismetSystemLibrary.PrintString(nil, 
            "HotReload FAILED: " .. moduleName .. " | " .. tostring(newModule))
        return false
    end
    
    return true
end

function HotReload.ReloadModules(moduleList)
    local results = {}
    for _, moduleName in ipairs(moduleList) do
        results[moduleName] = HotReload.ReloadModule(moduleName)
    end
    return results
end

return HotReload
```

#### 策略 C — Lua VM 重启（最后手段）

```
1. 保存关键游戏状态到 C++ 侧（玩家数据、进度等）
2. 关闭 UnLua 环境：FUnLuaDelegates::OnLuaStateClose.Broadcast()
3. 挂载补丁 Pak
4. 重新初始化 UnLua：FUnLuaDelegates::OnLuaStateCreated.Broadcast()
5. 从 C++ 侧恢复游戏状态
6. 重新进入游戏
```

### 6.3 Lua 热更的已知陷阱

| 陷阱 | 说明 | 解决方案 |
|------|------|---------|
| **闭包捕获** | 旧版本的函数可能被闭包持有引用，重载模块后闭包仍执行旧代码 | 避免在长生命周期闭包中直接捕获模块函数；改用间接调用 `Module.Func()` |
| **元表残留** | `setmetatable` 设置的元表在模块重载后不会自动更新 | 使用 `__index` 指向模块表本身，而非缓存函数引用 |
| **全局污染** | `_G.SomeVar = xxx` 设置的全局变量不会因模块重载而清除 | 禁止使用全局变量；使用模块级 local 变量 |
| **C++ 侧缓存的 Lua 函数引用** | C++ 通过 `luaL_ref` 保存的函数引用在重载后指向旧函数 | 重载后重新获取并更新引用 |
| **require 循环依赖** | A require B, B require A，重载 A 时 B 的缓存仍指向旧 A | 按依赖拓扑排序重载，被依赖者先重载 |
| **协程中的旧代码** | 正在执行的 coroutine 使用的函数不会被替换 | 等待协程完成后再重载，或放弃正在执行的协程 |

---

## 七、Shader 与 AssetRegistry 热更

### 7.1 Shader Library 加载

```
HotPatcher 的 Shader 热更流程：

1. Cook 阶段：HotPatcher 将变更材质的 ShaderBytecode 打入补丁 Pak
   补丁 Pak 内含：{ProjectName}/Content/ShaderArchive-{PlatformName}.ushaderbytecode

2. 运行时加载：
   UFlibPakHelper::LoadShaderbytecode(LibraryName, LibraryDir);
   
   内部调用 FShaderCodeLibrary::OpenLibrary() 注册新的 Shader Library

3. 后续材质加载时，引擎自动从新注册的 Shader Library 中查找编译产物

注意：
  - Android 平台的 ShaderPlatform 通常是 GLSL_ES3_1_ANDROID 或 VULKAN_ES3_1_ANDROID
  - 如果基础包和补丁包的 Shader 编译选项不一致，会导致 PSO 不匹配
  - 建议补丁和基础包使用完全相同的 Cook 参数
```

### 7.2 Asset Registry 增量合并

```
作用：让引擎"知道"补丁 Pak 中新增/变更的资源存在

加载方式：
  UFlibPakHelper::LoadAssetRegistry(RegistryName, LibraryDir);

原理：
  - 基础包自带完整的 AssetRegistry.bin
  - 补丁 Pak 中包含增量 AssetRegistry，只记录变更的资源
  - 运行时调用 LoadAssetRegistry 将增量部分合并到引擎的全局 AssetRegistry
  - 合并后，UAssetManager::Get().ScanPathsSynchronous() 等 API 能正确发现新资源

如果不加载增量 AssetRegistry：
  - 通过硬路径 LoadObject/LoadClass 仍可加载（Pak 文件系统直接查找）
  - 但 AssetManager、数据表扫描、蓝图引用发现等依赖 Registry 的功能会失效
```

---

## 八、下载系统设计

### 8.1 下载任务队列

```
场景：需要下载多个补丁包（增量策略下）

┌──────────────────────────────────────────────┐
│              DownloadQueue                    │
│                                              │
│  Task 1: patch_1.0.1.pak  [████████░░]  80%  │
│  Task 2: patch_1.0.2.pak  [░░░░░░░░░░]  等待  │
│  Task 3: patch_1.0.3.pak  [░░░░░░░░░░]  等待  │
│                                              │
│  总进度: 27%  |  速度: 2.3 MB/s  |  剩余: 45s │
└──────────────────────────────────────────────┘

串行下载（推荐）：
  - 一次只下载一个 Pak，完成后立即校验
  - 校验通过后再开始下一个
  - 避免并发写入导致的存储 I/O 瓶颈（Android 存储性能有限）

总进度计算：
  TotalProgress = (已完成文件总大小 + 当前文件已下载) / 所有文件总大小
```

### 8.2 断点续传

```
DownloadToolkit 的断点续传机制：

暂停：
  UDownloadProxy::Pause()
  → CancelRequest()，保留 TotalDownloadedByte
  → 文件不删除（已追加写入到磁盘）

恢复：
  UDownloadProxy::Resume()
  → 重新发起 GET 请求
  → Range: bytes={TotalDownloadedByte}-{FileSize}
  → 继续 FILEWRITE_Append 追加写入
  → MD5 计算从上次状态继续

应用退出后的续传：
  · DownloadToolkit 本身不支持跨进程续传（MD5 中间状态未持久化）
  · 需要在 UpdateManager 层实现：
    1. 记录下载进度到 pending/download_state.json
    2. 重启后检查 pending/ 目录中的临时文件
    3. 对已有文件执行全量 MD5 校验
    4. 如果文件不完整，使用 Range 请求继续下载
    5. 如果文件损坏，删除后重新下载
```

### 8.3 网络异常处理

| 场景 | 处理方式 |
|------|---------|
| **网络断开** | 自动暂停下载，监听网络恢复事件后自动 Resume |
| **下载超时** | DownloadToolkit 默认 5 秒超时（SetTimeout），触发 OnDownloadComplete(Failed)，自动重试 |
| **HTTP 4xx/5xx** | 记录错误码，提示用户"服务器维护中"，定时重试 |
| **存储空间不足** | 下载前检查 `FPlatformMisc::GetDiskTotalAndFreeSpace()`，预留 1.5x 补丁大小 |
| **切到后台** | Android Activity onPause → 暂停下载；onResume → 恢复下载 |
| **WiFi → 4G 切换** | 弹窗提示"当前使用移动数据，是否继续下载？" |

---

## 九、错误处理与回滚

### 9.1 错误分级

```
Level 0 — 可自动恢复
  · 网络抖动 → 自动重试
  · 单个分片校验失败 → 重新下载该分片

Level 1 — 需要用户交互
  · 多次重试仍失败 → 提示用户检查网络
  · 存储空间不足 → 提示用户清理空间
  · WiFi → 移动数据 → 确认是否继续

Level 2 — 需要回滚
  · Pak 挂载失败 → 删除该 Pak，恢复到上一版本
  · Lua 重载崩溃 → 捕获异常，恢复旧模块
  · 补丁导致游戏无法启动 → 下次启动时检测异常，清除所有补丁

Level 3 — 不可恢复
  · 基础包与补丁版本不兼容 → 提示用户重新安装游戏
```

### 9.2 回滚机制

```
回滚策略：

  客户端自动回滚（启动保护）：

  1. 每次启动时写入 launch_flag = "starting"
  2. 成功进入主界面后更新为 launch_flag = "running"
  3. 如果连续 N 次（建议 N=2）启动时发现 launch_flag = "starting"
     → 说明上次启动崩溃了
     → 自动卸载最近安装的补丁 Pak
     → 回退 version.json 到上一版本
     → 上报崩溃信息到服务器

  服务端回滚：

  1. 管理后台标记某版本为"已回滚"
  2. 客户端版本检查时发现 currentVersion 已被回滚
  3. 服务器返回 rollbackTo 字段
  4. 客户端卸载指定版本之后的所有补丁
```

### 9.3 崩溃保护流程

```
正常启动：
  write("starting") → 挂载补丁 → 进入游戏 → write("running")

崩溃场景：
  第 1 次：write("starting") → 挂载补丁 → 崩溃
  第 2 次：发现 flag="starting"（连续 1 次）→ 正常继续 → 崩溃
  第 3 次：发现 flag="starting"（连续 2 次）→ 触发回滚 → 卸载最新补丁 → 尝试启动
```

---

## 十、Android 平台特殊考虑

### 10.1 存储路径

```
Android 上 UE 的文件路径映射：

FPaths::ProjectDir()
  → /storage/emulated/0/UE4Game/{ProjectName}/

FPaths::ProjectSavedDir()
  → /storage/emulated/0/UE4Game/{ProjectName}/Saved/

FPaths::ProjectContentDir()
  → 打包后实际在 APK/OBB 内部，不可写

补丁文件存储位置（推荐）：
  /storage/emulated/0/UE4Game/{ProjectName}/Saved/HotUpdate/installed/

注意：
  · Android 10+ (Scoped Storage) 限制了外部存储访问
  · 使用 getExternalFilesDir() 或 App 内部存储更安全
  · UE 5.x 默认使用 App 私有目录，通常不受 Scoped Storage 限制
```

### 10.2 OBB 与 Pak 的关系

```
Android 打包结构：

标准打包（无分包）：
  APK
  └── assets/
      └── {ProjectName}.pak          ← 基础包 Pak（随 APK 安装）

Google Play 分包（App Bundle / OBB）：
  APK（代码 + 少量资源）
  OBB（main.{version}.{package}.obb）← 实质上是一个 Pak 文件

热更补丁 Pak 与基础 Pak 并存：
  引擎加载顺序：APK 内 Pak (Order=0) → 补丁 Pak (Order=1,2,3...)
  补丁中同路径资源覆盖基础包资源
```

### 10.3 内存与性能

```
关键约束：

1. 内存：
   · 每个挂载的 Pak 会占用一定内存（文件索引表）
   · 大量小 Pak 比少量大 Pak 消耗更多内存
   · 建议：单次热更合并为一个 Pak，累积不超过 5-10 个补丁 Pak

2. I/O 性能：
   · Pak 越多，文件查找链越长（需遍历所有 Pak 的索引）
   · Android 闪存随机读取性能远低于 PC
   · 建议：版本积累过多时，服务端下发合并的全量补丁

3. 安装耗时：
   · MountPak 本身很快（毫秒级，只读索引不读内容）
   · LoadAssetRegistry 较慢（需解析序列化数据）
   · LoadShaderLibrary 中等
   · 建议：在 Loading 画面中执行，避免卡顿主线程
```

### 10.4 后台下载与通知

```
Android 后台下载注意事项：

1. Activity 生命周期：
   · onPause → 游戏进入后台 → 应暂停下载
   · onResume → 恢复下载
   · UE 提供 FCoreDelegates::ApplicationWillDeactivateDelegate
               FCoreDelegates::ApplicationHasReactivatedDelegate

2. 大文件下载：
   · 如果补丁较大（>50MB），考虑使用 Android DownloadManager 系统服务
   · 系统服务支持通知栏进度、断点续传、后台下载
   · 但需要 JNI 桥接，与 DownloadToolkit 并行维护

3. 电量与温控：
   · 持续下载会增加功耗
   · 建议在 WiFi + 充电状态下自动启动大补丁下载
```

---

## 十一、安全考虑

### 11.1 传输安全

```
1. 全链路 HTTPS
   · CDN 下载链接必须使用 HTTPS
   · 版本检查 API 使用 HTTPS + Certificate Pinning

2. 下载地址签名
   · CDN URL 携带时间戳签名，防止链接被篡改
   · URL 有效期建议 30 分钟
   · 示例：https://cdn.example.com/patch.pak?sign=xxx&expire=1718520000
```

### 11.2 文件完整性

```
1. MD5 校验（DownloadToolkit 内置）
   · 下载过程中边算边验

2. SHA256 校验（推荐增加）
   · MD5 理论上可碰撞，SHA256 更安全
   · 下载完成后二次校验

3. Pak 签名（UE 内置，可选）
   · UE 支持对 Pak 文件进行 RSA 签名
   · 引擎在挂载时自动验证签名
   · 配置：Project Settings → Packaging → Signing Key
```

### 11.3 Pak 加密

```
UE 的 Pak 加密支持：

1. AES-256 加密
   · 在 Project Settings → Crypto → Encryption Key 中配置
   · Pak 内容在打包时加密，运行时用密钥解密
   · 密钥编译到可执行文件中（有被逆向的风险）

2. HotPatcher 的加密支持
   · FExportPatchSettings 中可配置 bEncrypt = true
   · 补丁 Pak 使用与基础包相同的加密密钥
   · UFlibPakHelper::GetPakFileList(PakPath, AESKey) 支持传入密钥

3. Lua 脚本保护
   · .lua 文件打入 Pak 后随 Pak 加密
   · 额外建议：使用 LuaJIT bytecode 编译，增加逆向难度
   · 注意：bytecode 保护不等于加密，仅增加阅读难度
```

---

## 十二、测试策略

### 12.1 自动化测试矩阵

| 测试项 | 测试方法 | 通过标准 |
|--------|---------|---------|
| 版本检查 — 无更新 | Mock 服务器返回相同版本 | 直接进入游戏，无弹窗 |
| 版本检查 — 可选更新 | Mock 返回新版本 + forceUpdate=false | 弹窗提示，可跳过 |
| 版本检查 — 强制更新 | Mock 返回 + forceUpdate=true | 不可跳过，必须更新 |
| 下载 — 正常完成 | 实际 CDN 下载小型测试 Pak | 进度条正确，MD5 匹配 |
| 下载 — 断点续传 | 下载到 50% 时 Kill 进程，重启恢复 | 从断点继续，最终 MD5 正确 |
| 下载 — 网络断开 | 下载时拔网线/开飞行模式 | 暂停下载，恢复后继续 |
| 校验 — MD5 不匹配 | 篡改下载后的文件 | 检测到不匹配，自动重试 |
| 安装 — 正常挂载 | 正确的补丁 Pak | MountPak 返回 true，资源可加载 |
| 安装 — 挂载失败 | 损坏的 Pak 文件 | 触发回滚，版本不变 |
| Lua — 模块重载 | 补丁中包含修改的 .lua 文件 | 新逻辑生效，旧逻辑不残留 |
| 回滚 — 启动崩溃保护 | 制造启动崩溃 | 连续崩溃后自动卸载补丁 |
| Android — 存储不足 | 填满设备存储后尝试下载 | 提示空间不足，不崩溃 |
| Android — 后台切换 | 下载时切到后台再切回 | 下载暂停/恢复正常 |
| 多版本累积 | 从 1.0.0 连续更新到 1.0.5 | 所有补丁按序挂载，最新资源生效 |

### 12.2 HotPatcher 打包验证清单

```
出包前检查：
  ☐ bUseIoStore = False（DefaultGame.ini 中已确认）
  ☐ 基础包和补丁包使用相同的 Cook 参数
  ☐ 补丁 Pak 的 MountPoint 与基础包一致
  ☐ 补丁中包含变更资源的所有依赖
  ☐ AssetRegistry 增量文件已包含在补丁 Pak 中
  ☐ 如果含 Shader 变更，ShaderBytecode 已包含
  ☐ Pak 文件可在目标设备上正常 Mount
  ☐ 补丁 Pak 中的资源能正确覆盖基础包资源
  ☐ UnLua 脚本路径与运行时 require 路径一致
```

---

## 十三、CI/CD 集成建议

### 13.1 补丁生成流水线

```
┌──────────┐    ┌──────────┐    ┌───────────┐    ┌───────────┐    ┌──────────┐
│ Git Push │ →  │ Cook     │ →  │ HotPatcher│ →  │ 校验 &    │ →  │ 上传 CDN │
│ (补丁分支)│    │ 资源     │    │ 生成补丁  │    │ 签名      │    │ + 注册版本│
└──────────┘    └──────────┘    └───────────┘    └───────────┘    └──────────┘

HotPatcher Commandlet（CI 中使用）：
  UE5Editor-Cmd.exe {Project.uproject} 
    -run=HotPatcher 
    -config="{PatchConfig.json}"
    
PatchConfig.json 关键字段：
  · BaseVersion：基础版本 Release 导出的 JSON
  · bByBaseVersion：true（基于基础版本对比）
  · bCookPatchAssets：true（Cook 变更资源）
  · bStorageNewRelease：true（保存新 Release 供下次对比用）
  · PakTargetPlatforms：["Android"]
```

---

## 附录 A：关键 API 速查

### HotPatcher Runtime

```cpp
// Pak 挂载
UFlibPakHelper::MountPak(const FString& PakPath, int32 PakOrder, const FString& MountPoint = "");
UFlibPakHelper::UnMountPak(const FString& PakPath);
UFlibPakHelper::GetAllMountedPaks() → TArray<FString>;

// Shader 加载
UFlibPakHelper::LoadShaderbytecode(const FString& LibraryName, const FString& LibraryDir);
UFlibPakHelper::CloseShaderbytecode(const FString& LibraryName);

// Asset Registry
UFlibPakHelper::LoadAssetRegistry(const FString& LibraryName, const FString& LibraryDir);

// 版本信息
UFlibPakHelper::GetPakFileList(const FString& InPak, const FString& AESKey = "");
UFlibPakHelper::ScanExtenPakFiles() → TArray<FString>;

// 挂载监听
UMountListener::Init();
UMountListener::OnMountPakDelegate;    // 多播委托
UMountListener::OnUnMountPakDelegate;  // 多播委托
```

### DownloadToolkit

```cpp
// 下载
UDownloadProxy* Proxy = NewObject<UDownloadProxy>();
Proxy->RequestDownload(URL, SavePath, bSlice, SliceByteSize);
Proxy->Pause();
Proxy->Resume();
Proxy->Cancel();

// 进度
Proxy->GetDownloadProgress() → float;       // 0.0 ~ 1.0
Proxy->GetDownloadSpeedKbs() → float;       // KB/s
Proxy->GetTotalDownloadedByte() → int64;

// 校验
Proxy->HashCheck() → FString;  // MD5 hex string

// 委托
Proxy->OnDownloadComplete;   // FOnDownloadComplete(EDownloadStatus)
Proxy->OnDownloadPaused;
Proxy->OnDownloadResumed;
Proxy->OnDownloadCanceled;
```

---

## 附录 B：常见问题与排查

| 问题 | 可能原因 | 排查方法 |
|------|---------|---------|
| 补丁资源不生效 | Pak MountOrder 低于基础包 | 检查 MountPak 的 Order 参数是否 > 0 |
| 补丁资源不生效 | 资源路径不匹配 | `UFlibPakHelper::GetPakFileList()` 打印补丁内路径 |
| 补丁资源不生效 | IoStore 模式 | 确认 `bUseIoStore=False` |
| 挂载后蓝图找不到新资源 | 未加载增量 AssetRegistry | 调用 `LoadAssetRegistry` |
| 材质变黑/粉红 | 未加载 ShaderBytecode | 调用 `LoadShaderbytecode` |
| Lua 更新后逻辑不变 | `package.loaded` 缓存未清 | 清除缓存后重新 `require` |
| 下载速度为 0 | CDN 域名解析失败 | 检查 Android 网络权限和 DNS |
| 下载中途失败 | 服务器不支持 Range 请求 | 确认 CDN 支持 `Accept-Ranges: bytes` |
| 安装后闪退 | Pak 内资源与 C++ 不兼容 | 确认基础包和补丁使用同一引擎版本编译 |
| Android 存储权限被拒 | Scoped Storage 限制 | 使用 App 私有目录 `getExternalFilesDir()` |
| 多个补丁互相覆盖错乱 | MountOrder 分配不正确 | 严格按版本号递增分配 Order |
