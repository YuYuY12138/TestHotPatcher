# UE 热更新系统总体设计（阶段性基线）

> 项目：UE 5.7 + UnLua + HotPatcher + DownloadToolkit（DTKit）  
> 目标：启动阶段完成资源更新，尽量不要求玩家重启 App  
> 重要边界：下载流程不走 Lua；Lua 只是可被更新的业务内容之一

---

## 1. 当前结论

当前方案已经具备完整热更新系统的主要骨架，可以进入源码核验和详细设计阶段。

核心结论：

- HotPatcher 负责补丁生产；
- DTKit 负责下载与断点续传；
- C++ 热更管理器负责版本比较、校验、安装、挂载和恢复；
- 启动阶段使用独立 Bootstrap / HotUpdate 关卡；
- 补丁挂载完成后，再初始化业务 Lua 和正式登录界面；
- 玩家可以不重启 App，在启动阶段完成更新；
- 新补丁完整成功前，旧版本必须保持可运行；
- MD5 只负责校验下载文件是否一致，不负责判断资源差异。

---

## 2. 系统职责划分

### 2.1 HotPatcher：补丁生产层

负责：

- 保存基础版本；
- 扫描当前工程资源；
- 对比基础版本和当前版本；
- 分析新增、修改、删除资源；
- 分析依赖；
- Cook 差异资源；
- 生成 Pak / IoStore 补丁；
- 输出版本信息。

核心职责：

> 判断哪些资源发生变化，并把变化资源制作成补丁。

Pak 模式通常输出：

```text
Patch_xxx.pak
```

IoStore 模式通常输出：

```text
Patch_xxx.pak
Patch_xxx.utoc
Patch_xxx.ucas
```

可能还包含：

```text
Patch_xxx.sig
Shader 文件
AssetRegistry
Lua / JSON / DB 等 Non-UFS 文件
```

---

### 2.2 DTKit：文件下载层

负责：

- HTTP 下载；
- HEAD 请求；
- Range 分片；
- 暂停；
- 恢复；
- 取消；
- 断点续传；
- 下载速度和进度；
- 文件落盘；
- Hash / MD5 计算。

核心职责：

> 稳定地把服务端补丁文件下载到客户端。

下载流程由 C++ 与 DTKit 完成，不走 Lua。

---

### 2.3 UHotUpdateSubsystem：流程调度层

建议实现为：

```cpp
UHotUpdateSubsystem : public UGameInstanceSubsystem
```

负责：

- 初始化目录；
- 恢复上次任务；
- 请求远端版本清单；
- 比较本地与服务器版本；
- 计算需要下载的补丁；
- 调度 DTKit；
- 校验 Size 与 MD5；
- 校验 IoStore 文件组；
- 安装补丁；
- 挂载 Pak / IoStore；
- 处理 Shader Library；
- 处理 AssetRegistry / AssetManager；
- 提交本地版本；
- 失败恢复与回滚；
- 通知启动流程进入业务初始化。

---

### 2.4 Lua：业务层

Lua 不负责：

- 版本检查；
- 下载；
- MD5 校验；
- 文件安装；
- Pak / IoStore 挂载。

正确关系：

```text
C++ 热更系统
→ 下载
→ 校验
→ 安装
→ 挂载
→ 初始化 Lua 业务
```

需要延后的是：

- Lua 业务入口执行；
- 业务模块 require；
- Lua UIManager；
- Lua 单例；
- Lua 委托、Timer、Observable 注册。

---

## 3. 启动关卡设计

不推荐：

```text
App 启动
→ 正式登录关卡
→ 登录 Widget / Lua / 材质已加载
→ 再检查更新
```

原因是挂载成功不代表内存中的旧对象自动刷新。

可能提前加载的内容包括：

- 登录 Widget Blueprint；
- 登录 Lua；
- 登录背景材质；
- DataTable / DataAsset；
- 字体、纹理；
- 公告和服务器列表；
- UIManager。

推荐新增或确认：

```text
L_Bootstrap
L_HotUpdate
L_Startup
```

Bootstrap 关卡只承载：

- 最小 World；
- 最小 GameMode；
- 最小 PlayerController；
- HotUpdateWidget；
- 热更流程。

建议仅包含：

```text
简单 Camera
简单背景
最小 GameMode
最小 PlayerController
HotUpdateWidget
```

不应包含：

- 正式角色；
- 正式 HUD；
- GAS；
- 正式 UIManager；
- 登录业务；
- 大量 DataAsset；
- GameFeature；
- 业务 Lua。

视觉上可以像登录界面，但技术上必须与正式登录系统分离。

---

## 4. UE 启动时序

大体时序：

```text
进程启动
→ Engine 初始化
→ 创建 GameInstance
→ GameInstance::Init
→ 创建 / 加载 World
→ 加载启动地图
→ 创建 GameMode / PlayerController
→ BeginPlay
```

### GameInstance::Init 阶段

此时：

- GameInstance 已存在；
- Subsystem 可以初始化；
- 可以读文件和版本；
- 可以准备网络；
- World / PlayerController / Viewport 不一定完全可用；
- 不适合依赖普通 UMG 和正式业务。

推荐：

```cpp
void UG01GameInstance::Init()
{
    Super::Init();

    UHotUpdateSubsystem* Subsystem =
        GetSubsystem<UHotUpdateSubsystem>();

    Subsystem->InitializeHotUpdate();
}
```

### Bootstrap BeginPlay 阶段

此时：

- UWorld 已存在；
- GameMode 已创建；
- PlayerController 已创建；
- Viewport 可用；
- 可以稳定创建 UMG；
- 可以启动完整热更任务。

推荐：

```cpp
void ABootstrapPlayerController::BeginPlay()
{
    Super::BeginPlay();

    CreateHotUpdateWidget();

    if (UHotUpdateSubsystem* Subsystem =
        GetGameInstance()->GetSubsystem<UHotUpdateSubsystem>())
    {
        Subsystem->StartUpdate();
    }
}
```

最终职责：

```text
GameInstance::Init
→ 初始化热更管理器

Bootstrap BeginPlay
→ 创建更新 UI
→ 启动热更
```

---

## 5. 总体启动流程

```text
App 启动
    ↓
引擎级启动画面（可选）
    ↓
创建 GameInstance
    ↓
初始化 UHotUpdateSubsystem
    ↓
加载 L_Bootstrap
    ↓
Bootstrap BeginPlay
    ↓
创建 HotUpdateWidget
    ↓
恢复上次未完成任务
    ↓
请求版本清单
    ↓
比较版本
    ├─ 无更新
    │    ↓
    │  业务初始化
    │
    └─ 有更新
         ↓
       检查网络和空间
         ↓
       DTKit 下载
         ↓
       Size + MD5 校验
         ↓
       安装补丁
         ↓
       运行时挂载
         ↓
       Shader / Registry 处理
         ↓
       提交本地版本
    ↓
初始化业务 Lua
    ↓
加载正式登录关卡
    ↓
销毁 Bootstrap 和更新 UI
    ↓
进入登录流程
```

---

## 6. 热更新状态机

建议状态：

```cpp
enum class EHotUpdateState : uint8
{
    None,
    Initializing,
    RecoveringPendingPatch,
    RequestingManifest,
    ComparingVersion,
    WaitingForUserConfirm,
    PreparingDownload,
    Downloading,
    Paused,
    Verifying,
    Installing,
    Mounting,
    Finalizing,
    Completed,
    Failed,
    Canceled
};
```

主流程：

```text
Initializing
    ↓
RecoveringPendingPatch
    ↓
RequestingManifest
    ↓
ComparingVersion
    ├─ 无更新 → Finalizing
    └─ 有更新
           ↓
    WaitingForUserConfirm
           ↓
    PreparingDownload
           ↓
      Downloading
           ↓
       Verifying
           ↓
       Installing
           ↓
        Mounting
           ↓
       Finalizing
           ↓
       Completed
```

---

## 7. 三种“比较”

### 7.1 HotPatcher 资源差异比较

发生在构建侧：

```text
基础版本资源清单
vs
当前工程资源状态
```

用于决定：

- 哪些资源新增；
- 哪些资源修改；
- 哪些资源删除；
- 哪些依赖变化；
- 补丁中应包含什么。

### 7.2 客户端版本比较

发生在客户端：

```text
本地基础包版本
本地 Patch 版本
服务端最新 Patch 版本
```

例如：

```text
基础包：1.0.0
本地 Patch：1.0.2
服务端 Patch：1.0.4
```

需要下载：

```text
1.0.3
1.0.4
```

### 7.3 MD5 文件比较

发生在下载完成后：

```text
服务端清单 MD5
vs
客户端下载文件 MD5
```

用于判断：

> 客户端文件是否与服务端发布文件一致。

MD5 不负责：

- 判断版本；
- 找出资源变化；
- 解 Pak；
- 比较 Pak 内部资源。

---

## 8. MD5 校验

MD5 不解开 Pak，而是直接读取整个文件的字节：

```text
打开 Patch.pak
→ 分块读取
→ 持续 Update
→ 文件结束
→ Final
```

服务端清单示例：

```json
{
  "name": "Patch_001.pak",
  "size": 104857600,
  "md5": "8e27be7d6154a1f9e90b26d39c1b42ab"
}
```

判断：

```text
Size 相同 + MD5 相同
→ 文件通过

Size 不同
→ 下载不完整

Size 相同但 MD5 不同
→ 内容错误或损坏
```

移动端应采用流式读取：

```text
每次读取 1MB / 4MB
→ Update MD5
→ 复用缓冲
→ 继续读取
```

### 断点续传风险

如果本地已有前 50MB，只续传后 50MB，就必须确认最终 Hash 覆盖完整文件。

最稳妥方式：

```text
断点续传结束
→ 从文件头重新流式扫描完整文件
→ 重新计算 MD5
```

后续重点检查 DTKit：

- Pause 是否保留 MD5 上下文；
- Resume 是否补算已有文件；
- 进程重启后如何恢复；
- 是否重新扫描完整文件；
- Final Hash 是否覆盖完整文件。

---

## 9. 服务端版本清单

建议至少包含：

```json
{
  "baseVersion": "1.0.0",
  "latestPatchVersion": "1.0.4",
  "minBaseVersion": "1.0.0",
  "forceUpdate": true,
  "restartRequired": false,
  "files": [
    {
      "name": "Patch_1.0.4.pak",
      "url": "https://cdn.example.com/Patch_1.0.4.pak",
      "size": 104857600,
      "md5": "..."
    }
  ]
}
```

建议字段：

- 基础包版本；
- 最新 Patch；
- 最低允许基础包版本；
- 平台和渠道；
- 文件列表；
- URL；
- Size；
- MD5 / SHA-256；
- 是否强制更新；
- 是否整包更新；
- 是否允许蜂窝网络；
- 是否要求重启；
- 清单签名；
- CDN 备用地址。

---

## 10. 下载与安装目录

不能直接把下载中的文件写入正式目录。

建议：

```text
Saved/HotUpdate/
├── Manifest/
├── Pending/
├── Installed/
└── Backup/
```

下载时：

```text
Pending/Patch_001.pak.download
Pending/Patch_001.utoc.download
Pending/Patch_001.ucas.download
```

校验成功后：

```text
Pending
→ Installed
```

或者最终移动到：

```text
Saved/Paks/
```

临时后缀用于防止半截文件在下次启动时被误识别为有效补丁。

---

## 11. Pak 与 IoStore

### Pak 模式

通常下载：

```text
Patch_001.pak
```

可能还有：

```text
Patch_001.sig
Shader
AssetRegistry
Non-UFS 文件
```

每个文件进行：

```text
Size + MD5
```

### IoStore 模式

必须把同名文件视为一组：

```text
Patch_001.pak
Patch_001.utoc
Patch_001.ucas
```

可能还有：

```text
Patch_001.sig
```

必须全部校验成功：

```text
pak 成功
utoc 成功
ucas 失败
→ 整组 Patch 失败
→ 不允许安装和挂载
```

---

## 12. 无重启更新

项目目标可以定义为：

```text
App 已启动
→ Bootstrap 关卡
→ 下载
→ 校验
→ 安装
→ 运行时挂载
→ 初始化业务 Lua
→ 加载登录关卡
```

这是“启动阶段运行时热更”。

玩家不需要重启进程，但正式资源是在补丁挂载后首次加载，因此更容易正确使用新版。

不建议把第一阶段目标定义成：

```text
战斗中任意替换全部已加载资源
```

因为：

```text
Mount 成功
≠ 已加载 UClass 自动刷新
≠ Widget 自动替换
≠ 材质自动重载
≠ Lua 闭包自动更新
```

---

## 13. 挂载设计

推荐：

```text
全部下载完成
→ 全部校验成功
→ 全部安装完成
→ 按版本和优先级统一挂载
```

例如：

```text
Base
Patch_1.0.1
Patch_1.0.2
Patch_1.0.3
```

新 Patch 必须有更高覆盖优先级。

完整挂载可能包括：

```text
挂载 Pak / IoStore
→ 加载 Shader Library
→ 追加 AssetRegistry
→ AssetManager ScanPaths
→ 验证资源可访问
```

不能只把完整流程理解为 `MountPak`。

---

## 14. 本地版本提交

错误流程：

```text
下载成功
→ 立即写本地版本
→ 后续 MD5 或挂载失败
```

正确流程：

```text
下载成功
→ 校验成功
→ 安装成功
→ 挂载成功
→ Shader / Registry 成功
→ 最后提交本地版本
```

应将更新视为事务：

```text
准备
→ 执行
→ 验证
→ 提交
```

提交前，旧版本必须可用。

---

## 15. 异常恢复

启动后先检查上次任务：

```text
启动
→ 扫描 Pending
→ 读取事务状态
```

可能出现：

- 下载中被杀；
- 校验中崩溃；
- 文件移动一半；
- 安装未完成；
- 挂载失败；
- 下载完成但版本未提交。

恢复策略：

```text
临时文件可续传
→ 继续下载

文件完整
→ 直接校验

远端版本变化
→ 删除旧任务并重下

安装未完成
→ 回滚或重装

挂载失败
→ 不提交版本
```

---

## 16. 移动端前后台

进入后台：

```text
暂停请求
→ Flush 文件
→ 保存已下载长度
→ 保存任务状态
```

回到前台：

```text
检查网络
→ 检查远端文件是否变化
→ Range 续传
```

若进程被系统杀死：

```text
下次启动
→ RecoveringPendingPatch
```

---

## 17. 失败策略

| 阶段 | 建议处理 |
|---|---|
| Manifest 请求失败 | 重试、切 CDN、按策略进入旧版本 |
| 基础包过低 | 跳应用商店整包更新 |
| 磁盘不足 | 提示清理 |
| 下载失败 | 断点续传 |
| Size 失败 | 重下文件 |
| MD5 失败 | 删除临时文件并重下 |
| IoStore 文件组不完整 | 整组失败 |
| 安装失败 | 保留旧版本 |
| 挂载失败 | 不提交版本，回滚或要求重启 |
| Shader 失败 | 阻止进入业务或降级 |
| AssetRegistry 失败 | 阻止依赖新增资源的业务进入 |

核心原则：

> 新补丁未完成“下载—校验—安装—挂载—提交”全过程前，旧版本始终保持可运行。

---

## 18. 对象生命周期

### 常驻

`UHotUpdateSubsystem` 跟随 GameInstance：

- 保存版本；
- 保存挂载信息；
- 跨关卡；
- 提供状态；
- 支持后台预下载；
- 处理前后台切换。

### 短生命周期

更新完成后销毁：

- Manifest 请求；
- 下载队列；
- DTKit Proxy；
- UpdateTask；
- HotUpdateWidget；
- Bootstrap World 对象。

原则：

```text
Subsystem 常驻
UpdateTask 短生命周期
```

---

## 19. Bootstrap 自身更新规则

Bootstrap 关卡和更新 UI 是热更系统自身依赖。

建议：

```text
Bootstrap 核心资源不参与普通在线热更
```

或者：

```text
Bootstrap 核心资源仅随整包更新
```

避免循环依赖。

---

## 20. 项目状态划分

```text
Bootstrap
    ↓
HotUpdate
    ↓
BusinessInitialize
    ↓
Login
    ↓
Lobby
    ↓
Gameplay
```

- Bootstrap：最小引擎和更新壳；
- HotUpdate：C++、DTKit、下载、校验、挂载；
- BusinessInitialize：UnLua、配置、UIManager、网络业务；
- Login：正式登录地图和登录逻辑。

---

## 21. 待确认事项

### 21.1 是否启用 IoStore

确认：

```text
Use Io Store
Use Pak File
Generate Chunks
Pak Signing
Encryption
```

### 21.2 DTKit MD5 续传逻辑

确认：

- Pause / Resume；
- 进程重启；
- 已有文件补算；
- 是否重扫完整文件。

### 21.3 实际挂载接口

确认项目使用：

```text
FPakPlatformFile
HotPatcher Runtime
IoDispatcher
IoStore Runtime
自研 Mount 管理器
```

### 21.4 Shader

确认：

- Shared Shader Library；
- 补丁 Shader 产物；
- 运行时加载接口；
- Android / iOS 差异。

### 21.5 AssetRegistry / AssetManager

确认：

- 新增资源发现；
- PrimaryAsset 重扫；
- AssetManager 缓存；
- GameFeature 注册。

### 21.6 UnLua 启动时机

确认：

- Lua VM 创建时间；
- 业务入口执行时间；
- 哪些蓝图提前触发 Lua；
- 是否能延后业务初始化。

### 21.7 回滚和撤回

确认：

- 是否保留旧 Patch；
- 是否支持补丁黑名单；
- 是否支持服务端撤回；
- 挂载失败后的回滚策略。

---

## 22. 推荐后续研究顺序

```text
1. 确认是否启用 IoStore
2. 检查 DTKit 断点续传后的 MD5
3. 找到项目真实挂载接口
4. 验证 HotPatcher Runtime
5. 确认 Shader 处理
6. 确认 AssetRegistry / AssetManager
7. 确认 UnLua 业务入口是否可延迟
8. 确认是否已有 Bootstrap 关卡
9. 设计本地事务记录和回滚
10. Android 真机验证完整链路
```

---

## 23. 最终架构基线

```text
HotPatcher
→ 版本差异分析、Cook、补丁构建

DTKit
→ 下载、断点续传、进度、Hash

UHotUpdateSubsystem
→ 版本检查、调度、校验、安装、挂载、恢复、提交

Bootstrap 关卡
→ 承载启动阶段热更

MD5
→ 校验下载文件一致性

Pak
→ 下载和校验 Pak 文件

IoStore
→ 下载和校验 pak + utoc + ucas 文件组

UnLua
→ 挂载完成后初始化业务

登录
→ 热更成功后加载

用户体验
→ 无需重启 App

安全原则
→ 新补丁未完全成功前，旧版本保持可运行
```

---

## 24. 最终流程总图

```text
【构建侧】

基础版本
    ↓
HotPatcher 差异分析
    ↓
Cook 差异资源
    ↓
生成 Pak 补丁
    ↓
计算 Size + MD5
    ↓
生成版本清单
    ↓
上传 CDN / OSS 


【客户端】

App 启动
    ↓
GameInstance 初始化
    ↓
HotUpdateSubsystem 初始化
    ↓
加载 PreLogin 关卡
    ↓
创建更新 UI
    ↓
恢复未完成任务
    ↓
请求远端清单
    ↓
版本比较
    ├─ 无更新 → 业务初始化
    └─ 有更新
         ↓
       DTKit 下载到 Pending
         ↓
       Size 校验
         ↓
       MD5 校验
         ↓
       安装到正式目录
         ↓
       挂载
         ↓
       Shader / Registry 处理
         ↓
       验证资源
         ↓
       提交本地版本（Version记录）
         ↓
       清理临时文件
    ↓
初始化 UnLua 业务
    ↓
加载正式登录关卡
    ↓
销毁 Bootstrap 更新界面
    ↓
进入登录流程
```

---

## 文档状态

当前版本为阶段性总体设计。

后续每确认一个实现细节，应同步修正：

- 状态机；
- 流程图；
- 下载目录；
- 挂载策略；
- 回滚策略；
- Android / iOS 差异；
- Pak / IoStore 分支；
- UnLua 初始化时机。
