## UnLua 初始化时机详解

### 概述

UnLua 的初始化不是一个单一事件，而是分为多个阶段依次发生。从引擎启动到业务 Lua 代码真正运行，中间经历了模块加载、VM 创建、扩展库注册、蓝图绑定、业务入口执行等多个步骤。理解每个阶段发生的时间和触发条件，是热更新设计中"补丁必须在哪个时间点之前挂载"这一问题的基础。

---

### 第一阶段：UnLua 模块加载（引擎启动早期）

UnLua 作为一个 UE 插件模块，在引擎启动过程中由模块管理器加载。加载时机取决于 .uplugin 文件中配置的 LoadingPhase。

引擎启动时的模块加载顺序大致是：

```
引擎核心模块（Core、CoreUObject、Engine 等）
→ 引擎插件模块
→ 项目插件模块（按 LoadingPhase 排序）
→ 项目游戏模块（G01）
```

UnLua 模块的 StartupModule 被调用时，它做的事情主要是：注册各种引擎回调（用于监听 UObject 创建、蓝图编译、地图加载等事件），以及初始化 UnLua 的内部管理器。但此时它不会创建 Lua VM。

关键点：模块加载阶段只是让 UnLua 系统就绪，准备好在需要时创建 Lua VM。此阶段不执行任何 Lua 代码。

---

### 第二阶段：G01 游戏模块加载（紧随插件之后）

G01 的游戏模块在 UnLua 模块之后加载。G01.cpp 中的 StartupModule 做了一件关键的事情：

```
void FG01GameModule::StartupModule()
{
    UnLua::FLuaEnv::OnCreated.AddStatic(&FG01GameModule::OnLuaEnvCreated);
}
```

这行代码注册了一个静态回调，监听 UnLua 的 Lua 环境创建事件。注意这只是注册回调，不是触发 VM 创建。回调的内容是在 VM 创建后注册 Lua 原生扩展库：

```
OnLuaEnvCreated 中注册的扩展库：
  lproject、ltls、lcrypt、ljson、lxml、lpeg、
  lpbc、lfixed、lmsgpack、lwsproto、lhparser、
  ltw2.core、ltw2.event
```

这些扩展库是 C++ 编译的原生模块，Lua 代码中通过 require 使用。它们必须在 VM 创建后、业务代码执行前被注册，否则 Lua 业务代码 require 这些库时会失败。

关键点：G01 模块加载阶段注册了 VM 创建的回调，但 VM 仍未创建。

---

### 第三阶段：GameInstance 创建（引擎初始化中期）

引擎初始化过程中会创建 GameInstance。G01 项目使用 UG01GameInstance 作为 GameInstance 类。

UG01GameInstance::Init 中做的事情：

```
注册全局 Ticker（用于驱动 LuaProjectLibrary 每帧 Tick）
```

此时 GameInstance 已存在，UGameInstanceSubsystem（包括未来的 UHotUpdateSubsystem）可以初始化。但 Lua VM 仍然没有被创建，因为还没有任何 UnLua 绑定的蓝图被加载。

关键点：GameInstance 创建后 Subsystem 可用，这是热更管理器初始化的正确时机。VM 仍未创建。

---

### 第四阶段：启动地图加载（触发 VM 创建的关键时刻）

引擎加载 DefaultGameMap 指定的启动地图时，会实例化该地图中的 GameMode、PlayerController 以及地图中放置的所有 Actor。

如果这些对象中有任何一个是绑定了 UnLua 的蓝图（即蓝图类实现了 IUnLuaInterface 并指定了 GetModuleName），UnLua 系统会在该对象的构造/初始化过程中检测到这一点，此时触发 Lua VM 的创建。

VM 创建的内部流程：

```
引擎加载某个蓝图 Actor
→ Actor 实例化，调用构造函数/PostInitializeComponents
→ UnLua 检测到该 UClass 实现了 IUnLuaInterface
→ UnLua 检查当前是否已有 Lua VM
→ 没有 → 创建 lua_State
→ 执行基础初始化（注册 UE 反射绑定、设置 package.path/searchers 等）
→ 广播 FLuaEnv::OnCreated 委托
→ G01 的 OnLuaEnvCreated 回调被触发
→ 注册所有 Lua 原生扩展库（lpbc、lcrypt 等）
→ VM 创建完成，可以执行 Lua 代码
→ UnLua 尝试绑定触发创建的那个 UObject 的 Lua 脚本
→ require 对应的 Lua 模块（例如 GM_DevelopGameMode.lua）
```

在 G01 项目中，根据 G01Help.md 的描述，有多个 GameMode 蓝图绑定了 UnLua：

```
GM_DevelopGameMode.lua   → 开发模式 GameMode
GM_HomeGameMode.lua      → Home 场景 GameMode
GM_CombatGameMode.lua    → 战斗场景 GameMode
```

以及多个 PlayerController 蓝图：

```
BP_DevelopPlayerController.lua
BP_NetworkPlayerController.lua
BP_HomePlayerController.lua
BP_CombatPlayerController.lua
```

当引擎加载启动地图时，该地图指定的 GameMode 蓝图被实例化，如果这个 GameMode 绑定了 UnLua，就会触发 VM 创建。这是 VM 创建的最常见触发点。

关键点：Lua VM 的创建时机取决于第一个 UnLua 绑定蓝图何时被加载。这个时间点不是代码显式控制的，而是由地图和蓝图的配置隐式决定的。

---

### 第五阶段：蓝图 Lua 绑定（VM 创建后立即开始）

VM 创建后，UnLua 会对当前正在加载的 UObject 执行 Lua 绑定。绑定过程：

```
获取蓝图的 GetModuleName（例如 "GameMode.GM_DevelopGameMode"）
→ 将模块名转换为文件路径（Script/GameMode/GM_DevelopGameMode.lua）
→ 通过 UE 文件系统读取该 .lua 文件
→ 在 Lua VM 中执行 require
→ 返回的 Lua table 作为该 UObject 的 Lua 绑定表
→ UObject 的生命周期回调（BeginPlay、Tick 等）将被转发到 Lua 表中的对应函数
```

这里的"通过 UE 文件系统读取"是热更新生效的核心机制。UE 文件系统在读取文件时会按 Pak 优先级查找，高优先级 Pak 中的同路径文件优先返回。

如果此时补丁 Pak 已挂载，require 到的就是新版本的 Lua 文件。如果补丁还没挂载，require 到的就是基础包中的旧版本。

关键点：蓝图绑定时的 require 是 Lua 文件首次被加载的时刻。一旦加载到 package.loaded 中就不会再重新读取文件。

---

### 第六阶段：GameInstance Lua 绑定与 ReceiveInit（业务入口）

当 UG01GameInstance 的蓝图也绑定了 UnLua 时，UnLua 会加载 GI_G01GameInstance.lua。随后引擎调用 GameInstance 的 Init 生命周期时，UnLua 将其转发为 Lua 侧的 ReceiveInit 调用：

```
GI_G01GameInstance:ReceiveInit()
    LuaHelper.DisableGlobalVariable()
    EventLoop.Startup()         → require "Core.EventLoop"
    UEHelper.Initialize(self)   → require "Core.UEHelper"
    UIManager.Initialize()      → require "UI.UIManager"
                                → require "UI.Core.UIConfigSystem"
                                → require "UI.Core.UILayerSystem"
                                → require "UI.UIViewBase"
                                → require "UI.UIControllerBase"
                                → ...
    NetManager.Initialize()     → require "Net.NetManager"
                                → require "Net.TcpClient"
                                → require "Core.ProtoDispatcher"
                                → require "Core.Channel"
                                → ...
```

这个函数的执行会触发大量的 require 调用，把整个 Lua 框架体系加载到内存中。这是 Lua 业务的真正起点。

此后 GameMode 的 StartPlay 触发 Lua 侧的 OnStartPlay，注册 UI、初始化 proto 环境、连接服务器等业务流程开始。

关键点：ReceiveInit 是 Lua 业务代码大规模加载的起点。在此之前只有 GameMode 的 Lua 脚本被加载过。在此之后，package.loaded 中会有几十个甚至上百个 Lua 模块。

---

### 完整时间线总结

```
时间线                        事件                           Lua VM 状态
─────────────────────────────────────────────────────────────────────
引擎启动                      加载核心模块                    不存在
    │
    ├── UnLua 模块加载         注册引擎回调                    不存在
    │
    ├── G01 模块加载           注册 OnCreated 回调             不存在
    │
    ├── 创建 GameInstance      Init、Subsystem 初始化          不存在
    │
    │   ┌─── ★ 热更管理器应在这个窗口工作 ───┐
    │   │    版本检查、下载、校验、             │
    │   │    安装补丁、MountPak                │
    │   │    此时 VM 不存在，Lua 未加载         │
    │   └─────────────────────────────────────┘
    │
    ├── 加载启动地图           实例化 GameMode 蓝图              不存在
    │   │
    │   └── GameMode 绑定 UnLua                                ★ VM 创建
    │       │                                                  扩展库注册
    │       └── require GM_xxx.lua                             首个 Lua 文件加载
    │
    ├── PlayerController 绑定  require BP_xxxPlayerController   运行中
    │
    ├── GameInstance 绑定      require GI_G01GameInstance        运行中
    │   │
    │   └── ReceiveInit        大量 require                     运行中
    │       ├── EventLoop                                      package.loaded 快速增长
    │       ├── UIManager
    │       ├── NetManager
    │       └── ...
    │
    ├── GameMode:StartPlay     OnStartPlay                      运行中
    │   ├── 注册 UI
    │   ├── 初始化 proto
    │   └── 业务逻辑启动
    │
    └── 游戏正式运行           所有 Lua 模块已加载              运行中
```

---

### 对热更新设计的影响

从时间线可以看出，"创建 GameInstance" 和 "加载启动地图" 之间存在一个窗口期，此时 GameInstance 和 Subsystem 已可用，但 Lua VM 还没有被创建。热更新的全部流程（版本检查、下载、校验、安装、MountPak）必须在这个窗口期内完成。

实现方式是使用一个 Bootstrap 关卡作为启动地图，该关卡中所有对象都不绑定 UnLua，确保加载它时不会触发 VM 创建。热更流程在这个关卡中完成后，再通过 OpenLevel 切换到正式的登录关卡，此时正式关卡的 GameMode 绑定了 UnLua 才触发 VM 创建，require 到的全部是已挂载补丁中的最新 Lua 文件。

如果不使用 Bootstrap 关卡，而是直接加载正式的登录关卡作为启动地图，VM 会在登录关卡的 GameMode 实例化时创建，此时热更尚未完成，require 到的是旧版本 Lua 文件。后续即使挂载了补丁，已缓存在 package.loaded 中的旧模块不会被替换，只有重启进程或手动清理 package.loaded 才能让新版本生效。
