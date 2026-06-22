## UnLua VM 启动时间与业务初始化时间

### 两个时间点的区别

在热更新系统的设计中，UnLua 相关的初始化存在两个不同的时间点，它们的含义完全不同，必须区分清楚。

### UnLua VM 启动时间

UnLua VM 启动是指 Lua 虚拟机（lua_State）被创建的时刻。在 G01 项目中，这个时刻不是由业务代码主动控制的，而是由引擎自动触发。具体机制是：当引擎加载了第一个绑定了 UnLua 的蓝图资产时，UnLua 插件会自动创建 Lua VM。

G01 项目在 C++ 模块启动时注册了一个回调：

```
FG01GameModule::StartupModule
→ UnLua::FLuaEnv::OnCreated.AddStatic(&OnLuaEnvCreated)
```

这个回调在 Lua VM 被创建时触发，负责注册 Lua 原生扩展库（lpbc、lcrypt、ljson 等）。但回调本身不控制 VM 何时创建，它只是在 VM 创建后被通知。

VM 创建的实际触发条件是：引擎加载某个关卡或蓝图时，发现该蓝图类实现了 IUnLuaInterface（即绑定了 Lua 脚本），此时 UnLua 会检查 VM 是否存在，如果不存在就创建。

这意味着 VM 启动时间取决于引擎加载的第一个 UnLua 绑定蓝图是哪个。如果 Bootstrap 关卡的 GameMode 或 PlayerController 是一个绑定了 UnLua 的蓝图，那么 VM 会在 Bootstrap 加载时就被创建，这发生在热更新完成之前。如果 Bootstrap 关卡使用纯 C++ 的 GameMode 和 PlayerController，VM 就不会在这个阶段被创建，直到后续加载正式登录关卡时才触发。

VM 启动本身只是创建一个空的 Lua 环境并注册扩展库，不会执行任何业务 Lua 脚本。VM 启动后 Lua 环境处于就绪状态，但 package.loaded 是空的，没有任何业务模块被 require 过。

### UnLua 业务初始化时间

业务初始化是指 G01 的 Lua 业务入口被执行的时刻。具体来说是 GI_G01GameInstance.lua 中的 ReceiveInit 函数被调用：

```
GI_G01GameInstance:ReceiveInit()
→ LuaHelper.DisableGlobalVariable()
→ EventLoop.Startup()
→ UEHelper.Initialize(self)
→ UIManager.Initialize()
→ NetManager.Initialize()
```

这个调用链会通过 require 加载大量 Lua 模块，包括 UI 框架（UIManager、UIConfigSystem、UILayerSystem）、网络框架（NetManager、TcpClient、ProtoDispatcher）、事件系统（EventLoop、EventDispatcher）、工具库（LuaHelper、Log、TableEx）等。每一次 require 都会通过 UE 的文件系统去读取对应的 .lua 文件。

这就是热更新的关键所在：require 读取文件时经过的是 UE 的 Pak 文件系统。如果补丁 Pak 已经挂载，文件系统会优先从高优先级的补丁 Pak 中返回文件内容。因此只要业务初始化发生在补丁挂载之后，所有 require 到的 Lua 模块就自动是最新版本，不需要任何额外的重载操作。

反过来，如果业务初始化发生在补丁挂载之前，旧版本的 Lua 模块已经被 require 并缓存在 package.loaded 中，后续即使挂载了新补丁，这些模块也不会自动更新，必须手动清除 package.loaded 并重新 require，这就需要处理模块依赖顺序、全局状态残留、闭包引用等一系列复杂问题。

### 当前设计中两个时间点的位置

按照设计基线文档的启动流程，时序如下：

```
App 启动
→ 引擎初始化
→ 创建 GameInstance
→ UHotUpdateSubsystem 初始化
→ 加载 Bootstrap 关卡            ← ★ VM 不应在这里启动
→ 创建更新 UI（C++ 原生）
→ 版本检查
→ 下载补丁
→ 校验
→ 安装
→ 挂载补丁 Pak                   ← ★ 补丁在这里生效
→ 热更完成
→ 加载正式登录关卡               ← ★ VM 在这里启动（第一个 UnLua 蓝图被加载）
→ GI_G01GameInstance:ReceiveInit  ← ★ 业务初始化在这里（require 读取的全是新版本）
→ 进入登录流程
```

VM 启动在补丁挂载之后，业务初始化在 VM 启动之后。这个顺序保证了 Lua 侧不需要做任何特殊处理就能使用最新版本的脚本。

### 如果顺序搞反了会怎样

假设 Bootstrap 关卡的 GameMode 蓝图绑定了 UnLua，时序变成：

```
→ 加载 Bootstrap 关卡
→ 引擎发现 GameMode 蓝图绑定了 UnLua
→ 创建 Lua VM                    ← VM 提前启动了
→ 执行 GameMode 对应的 Lua 脚本   ← 旧版本的 Lua 被 require 了
→ ...热更流程...
→ 挂载补丁 Pak                   ← 新 Lua 文件进入文件系统
→ 但已经 require 过的模块不会重新加载
→ 内存中仍然是旧版本
```

这种情况下要让新版本生效就必须清除 package.loaded 中受影响的模块并重新 require，或者干脆重启 Lua VM。这不仅实现复杂，而且有全局状态残留、闭包引用旧函数、元表不一致等一系列风险。

### 设计要求

综合以上分析，当前设计对 Bootstrap 关卡有一条硬性要求：Bootstrap 关卡中使用的 GameMode、PlayerController、以及关卡中放置的所有 Actor，都必须是纯 C++ 类或不绑定 UnLua 的蓝图。不能使用任何实现了 IUnLuaInterface 的蓝图派生类。这样可以确保在 Bootstrap 阶段加载关卡时不会触发 Lua VM 的创建，从而保证 VM 启动和业务初始化都发生在补丁挂载之后。

更新 UI（进度条、弹窗等）同样不能使用 Lua 驱动的 Widget，必须用 C++ 直接创建 UMG 控件或者使用不绑定 UnLua 的蓝图 Widget。

只有在热更流程走完、Completed 状态之后加载正式登录关卡时，才允许第一个 UnLua 绑定蓝图被加载，此时 VM 创建、扩展库注册、业务 ReceiveInit 依次发生，所有 require 到的 Lua 文件都来自最新的补丁 Pak。
