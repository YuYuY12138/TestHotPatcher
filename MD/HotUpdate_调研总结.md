# G01 Android 热更新方案调研总结

> 项目：G01（UE 5.7 + UnLua + Java 服务器）
> 目标平台：Android
> 日期：2026-06-17

---

## 一、调研目标

验证 HotPatcher 插件在 UE 5.7 + Android 上的可行性，为 G01 项目的 Android 热更新打通技术路径。G01 的业务逻辑主要由 Lua（UnLua）承载，C++ 层作为能力底座相对稳定，因此热更新的核心场景是 **Lua 脚本 + UE 资源（蓝图、UI、材质、数据表等）的运行时替换**。

---

## 二、G01 项目与热更新的关系

### 2.1 可热更的内容

| 内容类型 | 所在位置 | 热更方式 |
|---------|---------|---------|
| Lua 脚本 | `Script/` 目录（UI、网络、玩法、Model、Controller 等） | 打入补丁 Pak → 挂载后 Pak 文件系统优先级覆盖 → UnLua require 加载新脚本 |
| UE 资源 | `Content/`（Widget 蓝图、材质、地图、数据表等） | 打入补丁 Pak → MountPak 覆盖基础包同路径资源 |
| 配置文件 | `Config/DataTable/` 下的 CSV、`Development/` 下的 proto/json | 打入补丁 Pak 或作为外部文件下载 |
| Shader 编译产物 | Cook 时生成的 ShaderBytecode | 打入补丁 Pak → 调用 LoadShaderbytecode 加载 |

### 2.2 不可热更的内容

| 内容类型 | 原因 |
|---------|------|
| C++ 代码（libUnreal.so） | 需重新编译原生二进制，必须重新出包上架 |
| C++ 注册的 Lua 原生扩展库（lpbc、lcrypt 等） | 编译进 .so，随 C++ 一起 |
| AndroidManifest / Gradle 配置 | 属于 APK 安装包结构 |
| 引擎核心模块 | 与 .so 绑定 |

### 2.3 G01 热更新的优势

G01 的架构天然适合热更新：

- **Lua 层承载了几乎所有业务逻辑**（UI 框架、网络协议、玩法系统、Model/Controller），这些都在 `Script/` 目录下，可以整体打包进补丁 Pak
- **C++ 层只做底座**（UnLua 接入、原生扩展库注册、GameInstance/GameMode 基类、AssetSubsystem），变更频率低，基础包发布后基本不需要改动
- **UnLua 的 require 机制**天然支持 Pak 文件系统优先级覆盖——高优先级 Pak 中的同路径 Lua 文件会自动替代基础包中的版本

---

## 三、已完成工作

### 3.1 HotPatcher 插件 UE 5.7 编译适配

HotPatcher 原生支持到 UE 5.4，放入 UE 5.7 项目无法编译。共修改 14 处，按风险分类：

**高风险改动（2 处）**：

| 改动 | 说明 |
|------|------|
| `HotPatcherPackageWriter` 新增 6 个纯虚函数实现 | UE 5.7 在 `ICookedPackageWriter` 接口中新增了 `CreateLinkerArchive`、`CreateLinkerExportsArchive` 等 6 个函数。参考引擎源码 `BasePackageWriter.cpp` 实现。**关键：返回值必须非空，否则打包断言崩溃** |
| `HotPatcherCommandlet` 退出前重置 cook flag | HotPatcher 设置了 `PRIVATE_GIsRunningCookCommandlet = true` 但未重置，导致 UE 5.7 引擎退出时 `UWorldPartition::BeginDestroy` 触发断言 |

**低风险改动（12 处）**：均为 API 等价替换（函数重命名、参数类型变更、include 补充等），不影响功能逻辑。详见 `HotPatcher_UE57_适配记录.md`。

### 3.2 DownloadToolkit 插件 UE 5.7 编译适配

共修改 6 处：

| 改动 | 说明 |
|------|------|
| OpenSSL → UE 内置 FMD5 | 移除 OpenSSL 依赖，改用引擎内置 `FMD5`，同时修复 Android clang 的 `-Wreturn-stack-address` 编译错误 |
| HackHttpResponsePayload 移除 | UE5 的 `GetContent()` 已返回引用，不再需要 union hack |
| FTicker → FTSTicker | UE 5.7 API 变更 |
| OnRequestProgress → OnRequestProgress64 | 进度回调参数 int32 → uint64 |
| 移除 Slate 依赖 + 删除示例 Content | 核心下载代码不需要 Slate，示例蓝图删除 |
| **构造函数重写（解决 Android 启动崩溃）** | 见下文 3.3 |

### 3.3 Android 启动崩溃修复

**现象**：Android 包启动即崩溃 `FMallocBinnedCommon attempted to GetAllocationSizeExternal on an unrecognized pointer`，Windows 正常。

**定位过程**：
1. 逐个排除插件 → 确认是 DownloadToolkit 导致
2. 使用 NDK `llvm-addr2line` 解析崩溃地址 → 确认崩溃在 DownloadProxy.cpp 编译单元内

**根因**：`UDownloadProxy` 构造函数调用 `Reset()`，`Reset()` 内部执行委托清理和内存操作。引擎启动早期创建 CDO 时调用此构造函数，Android 的内存分配器在此阶段对这些操作不兼容。

**修复**：构造函数改为 C++ 初始化列表直接赋默认值，不调用任何引擎子系统。

### 3.4 IoStore 兼容性

UE 5.7 默认 `bUseIoStore=True`，出的基础包是 `.ucas/.utoc` 格式，HotPatcher 的传统 `.pak` 补丁无法在其上生效。已在 `DefaultGame.ini` 中设置 `bUseIoStore=False`。

### 3.5 当前验证状态

| 项目 | 状态 |
|------|------|
| HotPatcher UE 5.7 编译 | ✅ 通过（Windows + Android） |
| DownloadToolkit UE 5.7 编译 | ✅ 通过（Windows + Android） |
| Android 基础包打包 | ✅ 正常 |
| Android 基础包运行 | ✅ 启动正常 |
| HotPatcher 编辑器中生成补丁 Pak | ✅ 可用 |
| Android 运行时 Pak 挂载 | 待验证 |
| Lua 脚本通过 Pak 覆盖生效 | 待验证 |

---

## 四、热更新系统整体架构

### 4.1 架构总览

```
┌───────────────────────────────────────────────────────┐
│                    Java 服务端                         │
│                                                       │
│  ┌──────────────┐  ┌──────────────┐                  │
│  │ 版本检查 API  │  │ CDN / OSS    │                  │
│  │ /check-update │  │ 补丁 Pak 存储 │                  │
│  │              │  │              │                  │
│  │ · 版本清单    │  │ · patch.pak  │                  │
│  │ · 强制更新    │  │ · MD5/SHA256 │                  │
│  │ · 灰度策略    │  │              │                  │
│  └──────┬───────┘  └──────┬───────┘                  │
│         │                 │                           │
│    服务端负责，需 Java 端同学配合                        │
└─────────┼─────────────────┼───────────────────────────┘
          │ HTTP API         │ HTTP 文件下载
          ▼                  ▼
┌───────────────────────────────────────────────────────┐
│                  Android 客户端（UE + UnLua）           │
│                                                       │
│  ┌─────────────────────────────────────────────────┐  │
│  │               热更新管理器                        │  │
│  │  版本检查 → 下载补丁 → MD5校验 → MountPak        │  │
│  │     → 加载 ShaderLib/AssetRegistry              │  │
│  │     → Lua 脚本自动生效（Pak 优先级覆盖）          │  │
│  └─────────────────────────────────────────────────┘  │
│                                                       │
│  ┌──────────────┐  ┌──────────────┐                  │
│  │ HotPatcher   │  │ DownloadTool │                  │
│  │ Runtime      │  │ kit          │                  │
│  │ · MountPak   │  │ · HTTP 下载   │                  │
│  │ · ShaderLib  │  │ · 断点续传    │                  │
│  │ · AssetReg   │  │ · MD5 校验    │                  │
│  └──────────────┘  └──────────────┘                  │
│                                                       │
│  客户端负责                                            │
└───────────────────────────────────────────────────────┘
```

### 4.2 客户端与服务端职责划分

| 职责 | 归属 | 说明 |
|------|------|------|
| 补丁 Pak 生成 | 客户端（HotPatcher） | 编辑器中或 CI 流水线用 HotPatcher 出补丁 |
| 补丁文件托管 | **服务端（Java + CDN/OSS）** | 补丁 Pak 上传到 CDN 或 OSS，Java 端管理版本清单 |
| 版本检查 API | **服务端（Java）** | 客户端启动时调 API 查询是否有新版本，返回补丁下载地址和校验信息 |
| 强制更新/灰度策略 | **服务端（Java）** | 按版本号、设备、渠道等维度控制更新策略 |
| 补丁下载 | 客户端（DownloadToolkit） | 从 CDN 下载 Pak 文件 |
| 校验 + 挂载 | 客户端 | MD5 校验 → MountPak → 加载 Shader/AssetRegistry |
| Lua 脚本生效 | 客户端（UnLua 自动） | Pak 挂载后，UnLua 的 require 自动从高优先级 Pak 读取新脚本 |
| 更新 UI | 客户端 | 进度条、提示弹窗等 |

### 4.3 服务端需要提供的能力（需 Java 端配合）

**版本检查接口**（建议方案）：

```
GET /api/v1/check-update?appVersion=1.0.0&patchVersion=1.0.1&platform=android

响应：
{
  "latestPatchVersion": "1.0.3",
  "forceUpdate": false,
  "patches": [
    {
      "version": "1.0.3",
      "pakUrl": "https://cdn.xxx.com/patches/1.0.3/patch.pak",
      "pakSize": 15728640,
      "md5": "a1b2c3d4...",
      "releaseNote": "修复战斗结算异常"
    }
  ]
}
```

**补丁文件托管**（建议方案）：
- 方案 A：阿里云 OSS / 腾讯云 COS，客户端直接从 OSS 下载，Java 端只管理版本清单
- 方案 B：Java 服务器自建文件服务，适合内网测试阶段
- 方案 C：Nginx 静态文件服务器 + Java 管理后台，兼顾简单和可控

---

## 五、本地开发阶段的服务器模拟方案

在 Java 服务端尚未就绪的阶段，客户端需要独立验证热更新流程。以下是三种模拟方式，按推荐顺序排列：

### 方案 1：Python 一行命令起本地 HTTP 服务器（推荐）

在 PC 上用 Python 启动一个静态文件服务器，模拟 CDN 下载：

```bash
# 在补丁目录下执行
cd D:\HotPatchServer
python -m http.server 8080
```

目录结构：
```
D:\HotPatchServer\
├── version.json          ← 模拟版本检查接口的响应
├── patches\
│   ├── patch_1.0.1.pak   ← HotPatcher 生成的补丁 Pak
│   └── patch_1.0.2.pak
```

`version.json` 内容：
```json
{
  "latestPatchVersion": "1.0.1",
  "forceUpdate": false,
  "patches": [
    {
      "version": "1.0.1",
      "pakUrl": "http://192.168.x.x:8080/patches/patch_1.0.1.pak",
      "pakSize": 15728640,
      "md5": "用 md5sum 命令算出来的值"
    }
  ]
}
```

客户端配置 PC 的局域网 IP（`192.168.x.x:8080`）即可测试完整的"版本检查 → 下载 → 校验 → 挂载"流程。

**优点**：最贴近真实场景，能验证完整的 HTTP 下载链路。
**要求**：手机和 PC 在同一局域网。

### 方案 2：adb push 直接推送补丁到手机（最快验证 Pak 挂载）

跳过下载环节，直接用 adb 把补丁 Pak 推到手机的指定目录：

```bash
adb push patch_1.0.1.pak /storage/emulated/0/UE4Game/G01/Saved/HotUpdate/installed/
```

游戏启动时扫描该目录，直接挂载 Pak。

**优点**：不需要网络和服务器，几秒内完成，专注验证 Pak 挂载和 Lua 覆盖是否生效。
**缺点**：跳过了下载和校验流程。

### 方案 3：UE 编辑器内直接挂载测试

在编辑器中通过蓝图或控制台命令直接调用 `UFlibPakHelper::MountPak()`，验证资源覆盖逻辑：

```
# UE 控制台
pak mount D:/Patches/patch_1.0.1.pak 1
```

**优点**：最快速的迭代，改完 Lua → 打补丁 → 挂载 → 立即看效果。
**缺点**：只验证资源覆盖逻辑，不验证 Android 真机行为。

**建议开发流程**：先用方案 3 在编辑器中快速迭代，确认补丁内容正确后用方案 2 在真机验证 Pak 挂载，最后用方案 1 验证完整的下载流程。

---

## 六、后续待推进事项

### 6.1 必须做（客户端侧，热更新基本能力）

| 序号 | 功能 | 说明 | 工作量 |
|------|------|------|--------|
| 1 | **Android 真机 Pak 挂载验证** | 用 adb push 补丁 Pak 到手机，验证 `MountPak` 后基础包资源被正确覆盖 | 1-2 天 |
| 2 | **Lua 脚本热更验证** | 补丁 Pak 中替换 `Script/` 下的 Lua 文件，验证 UnLua require 加载的是新版本 | 1 天 |
| 3 | **启动时自动挂载已安装补丁** | 游戏启动后扫描本地补丁目录，按版本顺序 MountPak，在进入 Lua 入口（`GI_G01GameInstance.lua ReceiveInit`）之前完成 | 1-2 天 |
| 4 | **版本检查 + 补丁下载流程** | 客户端向服务器请求版本信息，比对本地版本，下载新补丁并 MD5 校验 | 2-3 天 |
| 5 | **更新 UI** | 用 G01 现有的 UI 框架（UIManager + Controller/View）实现更新提示弹窗和下载进度条 | 2-3 天 |
| 6 | **本地版本持久化** | JSON 文件记录当前版本和已安装补丁列表 | 1 天 |

### 6.2 需要服务端配合（Java 端）

| 序号 | 功能 | 建议方案 | 备注 |
|------|------|---------|------|
| 1 | **版本检查 API** | Java 提供 REST 接口，返回最新版本号、补丁下载地址、MD5、是否强制更新 | 客户端开发阶段用本地 Python HTTP 服务器 + version.json 模拟 |
| 2 | **补丁文件托管** | 补丁 Pak 上传到 OSS/CDN，Java 端管理版本清单和下载地址生成 | 客户端开发阶段用 Python `http.server` 模拟文件下载 |
| 3 | **灰度/分渠道策略** | Java 端按设备ID/渠道/比例控制下发 | 初期不需要，正式上线后再加 |
| 4 | **服务端回滚** | Java 端标记某版本为"已回滚"，客户端检测后卸载 | 初期用发布新补丁覆盖旧补丁即可 |

### 6.3 建议做（提升稳定性）

| 功能 | 说明 |
|------|------|
| 下载失败自动重试 | 移动网络不稳定，建议 3 次重试 |
| 启动崩溃保护 | 连续崩溃 2 次自动卸载最新补丁，避免用户无法进入游戏 |
| 存储空间检查 | 下载前检查剩余空间，Android 低端机存储紧张 |
| Shader / AssetRegistry 增量加载 | 补丁含材质变更时需调用 LoadShaderbytecode 和 LoadAssetRegistry |

### 6.4 暂不需要

| 功能 | 原因 |
|------|------|
| 增量补丁 / 版本链 | 初期用全量补丁（每次基于基础包打差异）更简单，等补丁体积成为瓶颈再改 |
| Pak 加密 / 签名 | 内部测试阶段不需要，面向公网发布时再加 |
| CI/CD 自动出补丁 | 初期手动出补丁即可，频率上来后再搭建流水线 |
| 二进制差分（BinariesPatch） | 全量补丁体积可控的情况下不需要，复杂度高 |
| 跨进程断点续传 | DownloadToolkit 支持进程内续传，初期可接受"杀掉重下" |
| 后台下载 / 系统通知 | 需 JNI 桥接，复杂度高，初期前台 Loading 时下载即可 |

---

## 七、G01 Lua 热更新的关键要点

### 7.1 Lua 文件在 Pak 中的组织

G01 的 Lua 脚本在 `Script/` 目录下，打包时会被 Cook 到 Pak 中：

```
基础包 Pak 中：
  G01/Script/UI/UIManager.lua
  G01/Script/Net/NetManager.lua
  G01/Script/GamePlay/Combat/Combat.lua
  ...

补丁 Pak 中（只包含变更的文件）：
  G01/Script/UI/UIManager.lua          ← 修复了 UI 的 bug
  G01/Script/GamePlay/Combat/Combat.lua ← 调整了战斗数值
```

补丁 Pak 以更高优先级挂载后，UnLua 的 require 通过 UE 文件系统读取 Lua 文件时，自动从高优先级 Pak 中获取，无需额外的 Lua 层代码修改。

### 7.2 Lua 热更的注意事项

| 场景 | 说明 | 处理方式 |
|------|------|---------|
| Pak 挂载时机 | 必须在 `GI_G01GameInstance:ReceiveInit()` 之前完成 | 在 C++ 的 `UG01GameInstance::Init()` 中挂载补丁 Pak |
| 已加载的 Lua 模块 | `package.loaded` 中已缓存的模块不会自动更新 | 如果在游戏运行中挂载补丁（非启动时），需要清除 `package.loaded` 中对应模块的缓存 |
| 全局状态 | `_G` 中的全局变量不会因模块重载而改变 | G01 已通过 `LuaHelper.DisableGlobalVariable()` 禁止全局变量，风险较低 |
| 闭包引用 | 旧版本函数可能被闭包持有 | 避免在长生命周期闭包中直接捕获模块函数 |
| C++ 注册的 Lua 扩展 | `lpbc`、`lcrypt` 等编译在 .so 中 | 这些不可热更，但它们是底层库，几乎不需要改动 |

### 7.3 推荐的更新策略

对于 G01 项目，推荐**启动时全量更新**策略：

```
游戏启动
  → C++ Init() 中挂载本地已安装补丁
  → C++ Init() 中检查版本
  → 有新版本 → 显示更新 UI → 下载 → 校验 → 挂载
  → 无新版本 → 直接继续
  → UnLua 初始化（此时 Pak 已挂载，所有 Lua 文件已是最新版本）
  → GI_G01GameInstance:ReceiveInit()
  → 进入游戏
```

这样不需要处理运行时 Lua 模块重载的复杂性，因为所有 Lua 文件在首次 require 之前就已经是最新版本。

---

## 八、已知限制

| 限制 | 影响 | 应对 |
|------|------|------|
| C++ 代码不可热更 | 涉及 C++ 变更必须重新出包 | G01 的 C++ 层已经很薄且稳定，绝大部分业务在 Lua 中 |
| `bUseIoStore` 必须关闭 | 无法利用 IoStore 加载性能优化 | 对移动端影响不大 |
| 补丁 Pak 累积影响启动速度 | 每多一个 Pak 文件查找链多一层 | 建议累积 5 个以上补丁后出合并的全量补丁 |
| Lua 原生扩展库不可热更 | lpbc、lcrypt 等编译在 .so 中 | 这些是稳定的底层库，几乎不需要改动 |

---

## 九、建议推进顺序

```
第一步：验证 Pak 挂载 + Lua 覆盖（Android 真机）
  方式：adb push 补丁 Pak → 启动时自动挂载 → 确认 Lua 行为变化
  目标：确认核心技术路径可行
  预计：2-3 天

第二步：实现版本检查 + 下载流程
  方式：本地 Python HTTP 服务器模拟 → 客户端实现完整下载链路
  目标：打通"检测更新 → 下载 → 校验 → 挂载"
  等待：与 Java 端对齐版本检查 API 接口格式
  预计：3-5 天

第三步：更新 UI + 版本持久化
  方式：用 G01 现有 UIManager 框架实现
  目标：用户可感知的完整热更新体验
  预计：2-3 天

第四步：补充稳定性（崩溃保护、重试、空间检查）
  预计：2-3 天
```

第一步到第三步完成后即可支撑基本的 Lua + 资源热更新需求。
