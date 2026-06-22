## G01 项目结构体系介绍

本文基于当前 G01 仓库代码整理，重点说明客户端工程结构、UnLua 接入方式、Lua 框架分层、Source C++ 层职责，以及常见开发链路。服务器侧结构不在本文范围内。

## 一、项目整体定位

G01 当前可以理解为一个 **纯 UE 客户端 + UnLua Lua 业务层 + 自研网络协议层** 的客户端项目。

整体运行模式是：

```text
UE 蓝图 / C++ 基类
        ↓
UnLua 绑定 Lua 类
        ↓
Lua 框架基础设施
        ↓
UI / 网络 / 玩法业务
        ↓
自研协议与服务器通信
```

需要特别注意：当前客户端网络通信不是 UE Dedicated Server / Replication / UFUNCTION RPC 体系，而是通过 `NetManager.Send + ProtoDispatcher` 这种协议消息模式完成。

## 二、顶层目录概览

```text
G01/
├── Source/        -- UE C++ 模块、基类、Subsystem、原生辅助能力
├── Script/        -- Lua 业务与框架代码，项目主要逻辑所在地
├── Plugins/       -- 插件目录，包含 UnLua 等插件相关内容
├── Config/        -- UE 配置、Lua 调试配置等
├── Content/       -- UE 资源、蓝图、Widget、地图等
└── Development/   -- 开发期协议、配置或辅助资源
```

日常 Lua 业务开发主要看 `Script/`。涉及 UE 原生能力、Lua 扩展库注册、Subsystem、HTTP/WebSocket/资源加载等能力时看 `Source/`。

## 三、Source C++ 层结构

### 1. Source 顶层

```text
Source/
├── G01.Target.cs
├── G01Editor.Target.cs
└── G01/
```

`G01.Target.cs` 定义游戏目标：

```text
TargetType.Game
ExtraModuleNames.Add("G01")
```

`G01Editor.Target.cs` 定义编辑器目标：

```text
TargetType.Editor
ExtraModuleNames.Add("G01")
```

两者都使用：

```text
BuildSettingsVersion.V6
EngineIncludeOrderVersion.Unreal5_7
```

说明项目当前面向 UE 5.7 构建设置。

### 2. G01 C++ 模块

```text
Source/G01/
├── G01.Build.cs
├── G01.cpp
├── G01.h
├── Public/
└── Private/
```

`G01.Build.cs` 是模块依赖配置。公共依赖包括：

```text
Core
CoreUObject
Engine
InputCore
EnhancedInput
GameplayTags
```

私有依赖包括：

```text
UMG
Slate
SlateCore
Tw2Library
LuaLibrary
LuaProjectLibrary
LuaTlsLibrary
LuaCryptLibrary
LuaJsonLibrary
LuaXmlLibrary
LuaLPegLibrary
LuaPbcLibrary
LuaMsgpackLibrary
LuaFixedLibrary
LuaWSProtoLibrary
LuaHParserLibrary
LuaTw2Library
LuaExtension
UnLua
WebSockets
HTTP
```

这说明 C++ 层主要承担：

- UE 基础模块接入
- UMG/UI 支撑
- UnLua 接入
- Lua 扩展库接入
- HTTP/WebSocket 原生能力支持
- 资源加载 Subsystem 支持

### 3. Lua 扩展库注册：`Source/G01/G01.cpp`

`G01.cpp` 定义了项目主模块：

```cpp
IMPLEMENT_PRIMARY_GAME_MODULE(FG01GameModule, G01, "G01");
```

模块启动时监听 UnLua Lua 环境创建：

```cpp
UnLua::FLuaEnv::OnCreated.AddStatic(&FG01GameModule::OnLuaEnvCreated);
```

在 `OnLuaEnvCreated` 中注册 Lua built-in loader：

```text
lproject
ltls
Lcrypt
ljson
lxml
lpeg
lpbc
lfixed
lmsgpack
lwsproto
lhparser
ltw2.core
ltw2.event
```

这些就是 Lua 里可以直接 `require` 的原生扩展模块来源。例如：

```lua
local levent = require("ltw2.event")
local lpbc = require "lpbc"
local lcrypt = require("lcrypt")
local lproject = require("lproject")
```

因此，`G01.cpp` 是 **Lua 原生扩展能力注入点**。

## 四、Source/Public 与 Private 结构

```text
Source/G01/Public/
├── GamePlay/
├── Helper/
└── Subsystems/

Source/G01/Private/
├── GamePlay/
├── Helper/
└── Subsystems/
```

### 1. GamePlay C++ 基类

```text
Source/G01/Public/GamePlay/
├── G01GameInstance.h
├── G01GameModeBase.h
├── G01PlayerCharacter.h
└── G01PlayerController.h
```

对应实现：

```text
Source/G01/Private/GamePlay/
├── G01GameInstance.cpp
├── G01GameModeBase.cpp
├── G01PlayerCharacter.cpp
└── G01PlayerController.cpp
```

#### `UG01GameInstance`

继承自 `UGameInstance`。

关键职责：

- `Init()` 中注册全局 ticker
- `Shutdown()` 中移除 ticker
- 每帧调用 `FLuaProjectLibraryModule::Tick(L, DeltaTime)`
- `OnStart()` 中触发蓝图事件 `OnStartPlay()`

关键代码链路：

```text
UG01GameInstance::Init
    ↓
FTSTicker::GetCoreTicker().AddTicker
    ↓
UG01GameInstance::Tick
    ↓
FLuaProjectLibraryModule::Tick
```

这说明 C++ GameInstance 层负责驱动 LuaProjectLibrary 的 tick。

#### `AG01GameModeBase`

继承自 `AGameModeBase`。

关键职责：

- 重写 `StartPlay()`
- 调用蓝图事件 `OnStartPlay()`

```cpp
void AG01GameModeBase::StartPlay()
{
    Super::StartPlay();
    OnStartPlay();
}
```

Lua 侧 GameMode 可以实现对应的 `OnStartPlay`。

#### `AG01PlayerController`

继承自 `APlayerController`。

当前实现较薄，重写：

```text
BeginPlay
EndPlay
```

适合作为后续本地玩家侧初始化、输入、UI 启动等逻辑的基础类。

#### `AG01PlayerCharacter`

继承自 `ACharacter`。

当前保留 UE Character 标准生命周期：

```text
BeginPlay
Tick
SetupPlayerInputComponent
```

适合作为玩家角色基础类。

### 2. Subsystems

```text
Source/G01/Public/Subsystems/AssetSubsystem.h
Source/G01/Private/Subsystems/AssetSubsystem.cpp
```

`UAssetSubsystem` 继承自：

```cpp
UGameInstanceSubsystem
```

主要能力：

```text
RequestAsyncLoadAsset
ClearAllCachedAssets
ClearCachedAsset
IsAssetCached
GetCachedAsset
OnAssetLoaded
```

Lua UI 框架中 `UIConfigSystem` 会通过 `UEHelper.GetGameInstanceSubsystem(UE.UAssetSubsystem)` 获取它，用于 UI 资源预加载和缓存清理。

典型链路：

```text
UIConfigSystem.Start
    ↓
UEHelper.GetGameInstanceSubsystem(UE.UAssetSubsystem)
    ↓
RequestAsyncLoadAsset(viewPath)
```

### 3. Helper 原生辅助能力

```text
Source/G01/Public/Helper/
├── Http/
├── WebSocket/
└── LoadPackageAsync/
```

#### HTTP

```text
Helper/Http/HttpObject.h
Helper/Http/HttpUploadObject.h
Helper/Http/HttpDownloadObject.h
```

`UHttpObject` 继承：

```cpp
UObject, IUnLuaInterface
```

并通过：

```cpp
GetModuleName_Implementation() const override
{
    return TEXT("Helper.HttpObject");
}
```

暴露给 UnLua/Lua 使用。

它提供：

```text
HTTP 请求
上传
下载
HTTP Verb
Content-Type
Token 处理
```

Lua 侧也有对应辅助入口：

```text
Script/Core/HttpHelper.lua
Script/Helper/HttpObject.lua
```

#### WebSocket

```text
Helper/WebSocket/WebSocketObject.h
Helper/WebSocket/WebSocketObject.cpp
```

`UWebSocketObject` 封装 UE WebSocket，提供：

```text
Connect
Close
SendStringMessage
SendMessage
IsConnected
OnConnected
OnClosed
OnMessage
OnBinaryMessage
OnError
```

并保存 Lua 回调引用：

```text
refOnMessage
refOnConnected
refOnClosed
refOnError
refOnBinaryMessage
```

Lua 侧对应：

```text
Script/Core/WebSocket.lua
```

#### LoadPackageAsync

```text
Helper/LoadPackageAsync/LoadPackageAsync.h
Helper/LoadPackageAsync/LoadPackageAsync.cpp
```

用于异步包/资源加载辅助。

## 五、Script Lua 层结构

```text
Script/
├── GI_G01GameInstance.lua
├── GameMode/
├── GameObjects/
├── UI/
├── Net/
├── Core/
├── Utility/
├── GamePlay/
├── Development/
├── Config/
├── Define/
├── Helper/
└── Test/
```

Lua 层是当前项目主要业务所在地。

## 六、Lua 启动入口

### 1. `Script/GI_G01GameInstance.lua`

这是 Lua 全局入口。

初始化：

```lua
function M:ReceiveInit()
    LuaHelper.DisableGlobalVariable()
    EventLoop.Startup()
    UEHelper.Initialize(self)
    UIManager.Initialize()
    NetManager.Initialize()
end
```

关闭：

```lua
function M:ReceiveShutdown()
    NetManager.Shutdown()
    UIManager.Destroy()
    ProtoDispatcher.Cleanup()
    EventLoop.Shutdown()
    UEHelper.Shutdown()
end
```

职责：

- 启动 Lua 事件循环
- 保存 UE GameInstance
- 初始化 UI 框架
- 初始化网络框架
- 销毁全局系统
- 禁止非法全局变量

### 2. `Script/GameMode/`

```text
GM_DevelopGameMode.lua
GM_HomeGameMode.lua
GM_CombatGameMode.lua
```

当前 GameMode 负责不同开发/玩法场景的入口逻辑。

#### `GM_DevelopGameMode.lua`

开发模式入口，当前做：

```text
注册开发 UI
启动 UIManager
初始化 proto 环境
导入 proto 文件
初始化 message_id.json
```

#### `GM_HomeGameMode.lua`

Home 场景入口，当前做：

```text
SimulationServer.Initialize
注册 Home 相关事件
CAEnterHome 事件派发
```

#### `GM_CombatGameMode.lua`

战斗场景入口，当前做：

```text
CombatSystem.Initialize
SimulationServer.Initialize
注册战斗事件
CombatSystem.BeginPlay
CombatSystem.EndPlay
```

### 3. `Script/GameObjects/Player/`

```text
BP_DevelopPlayerController.lua
BP_NetworkPlayerController.lua
BP_HomePlayerController.lua
BP_CombatPlayerController.lua
BP_CombatPlayerCharacter.lua
```

这类文件是 UE PlayerController / Character 的 Lua 绑定层。

适合承载：

- 本地玩家初始化
- 输入模式设置
- 鼠标显示控制
- 本地 UI 启动
- 玩家对象生命周期

结合当前项目“纯 UE 客户端”定位，客户端 UI 更适合逐步迁移到 PlayerController 或 LocalPlayer 相关入口，而不是长期依赖 GameMode。

## 七、UnLua 使用模式

项目中同时存在两套类系统。

### 1. `UnLua.Class()`

用于绑定 UE 对象。

典型文件：

```text
GI_G01GameInstance.lua
GM_DevelopGameMode.lua
BP_DevelopPlayerController.lua
WBP_DevelopMain.lua
```

典型写法：

```lua
local M = UnLua.Class()
```

或继承 Lua View 基类：

```lua
local M = UnLua.Class("UI.UIViewBase")
```

常见生命周期：

```text
ReceiveInit
ReceiveShutdown
ReceiveBeginPlay
ReceiveEndPlay
ReceiveTick
Construct
Destruct
OnStartPlay
```

### 2. `LuaHelper.LuaClass()`

用于普通 Lua 逻辑类。

典型文件：

```text
UIControllerBase.lua
ModelBase.lua
NetworkController.lua
ActivityModel.lua
BagModel.lua
```

典型写法：

```lua
local M = LuaHelper.LuaClass("UI.UIControllerBase")
```

适合：

```text
Controller
Model
纯 Lua 业务对象
非 UE 生命周期对象
```

## 八、UI 框架体系

```text
Script/UI/
├── UIManager.lua
├── UIViewBase.lua
├── UIControllerBase.lua
├── UICommon.lua
├── Core/
├── Develop/
└── Widgets/
```

### 1. `UIManager.lua`

UI 对外统一门面。

常用接口：

```text
Initialize
Start
Destroy
RegisterConfig
State_Open
Dialog_Open
Lock_Open
MsgBox_OpenAlert
Toast_Open
CloseAll
```

业务层一般只通过 `UIManager` 管理 UI。

### 2. `UI/Core/UIConfigSystem.lua`

维护 UI 配置映射：

```text
UI 名称 -> Controller 类 -> Widget 蓝图路径
```

示例：

```lua
UIManager.RegisterConfig(
    "NetworkMain",
    "UI.Develop.Network.NetworkController",
    "/Game/Development/UI/WBP_Network.WBP_Network_C"
)
```

### 3. `UI/Core/UILayerSystem.lua`

负责创建和管理 UI 层级。

当前层级：

```text
State  = 200
Dialog = 201
Lock   = 202
MsgBox = 203
Toast  = 204
Top    = 205
```

含义：

```text
State  普通状态界面
Dialog 弹窗
Lock   锁屏/loading
MsgBox 消息框
Toast  飘字提示
Top    顶层 UI
```

### 4. `UIViewBase.lua`

Widget View 基类。

提供：

```text
Construct
Destruct
SubscribeEvent
UnsubscribeEvent
CreateEvent
TriggerEvent
SetVisible
BindOnClick
SetChildText
GetChildText
SetChildEnabled
```

View 主要负责：

- 绑定按钮/输入事件
- 设置控件显示
- 读取输入内容
- 向 Controller 抛事件

### 5. `UIControllerBase.lua`

Controller 基类。

负责：

- 持有 View
- 持有 Model
- 订阅 View 事件
- 处理业务逻辑
- 发网络请求
- 收协议回包
- 更新 View/Model
- 控制 UI 显隐和销毁

### 6. UI 推荐数据流

```text
View 点击按钮
    ↓
View TriggerEvent
    ↓
Controller 处理
    ↓
更新 Model / 发网络请求
    ↓
服务器回包或本地逻辑完成
    ↓
Controller 更新 Model / View
```

## 九、网络体系

```text
Script/Net/
├── NetManager.lua
├── TcpClient.lua
└── NetPack.lua

Script/Core/
├── Channel.lua
├── ProtoDispatcher.lua
└── EventDispatcher.lua
```

### 1. `NetManager.lua`

网络对外门面。

常用接口：

```text
Initialize
Shutdown
ConnectToServer
ReconnectToServer
Close
Send
```

业务层通常只调用：

```lua
NetManager.Send("CGXXXBuf", proto)
```

### 2. `TcpClient.lua`

负责真实网络流程：

```text
连接
握手
登录
重连
心跳
加密
拆包
收包分发
```

状态：

```text
CONNECTING
CONNECTING_HANDSHAKE
CONNECTED
DISCONNECTED
```

发送链路：

```text
业务 table
    ↓
NetPack.Pack
    ↓
AES 加密
    ↓
4 字节长度头
    ↓
Channel.Send
```

收包链路：

```text
Channel 收到 bytes
    ↓
按长度拆包
    ↓
AES 解密
    ↓
NetPack.Unpack
    ↓
ProtoDispatcher.DispatchMessage
```

### 3. `NetPack.lua`

负责：

```text
协议名 <-> message id <-> protobuf bytes
```

依赖：

```text
lpbc
message_id.json
GameBaseProtobufMessage
```

### 4. `ProtoDispatcher.lua`

按协议名分发服务器回包。

注册：

```lua
ProtoDispatcher.AddDispatch("GCXXXBuf", self, self.OnGCXXXBuf)
```

移除：

```lua
ProtoDispatcher.RemoveDispatch("GCXXXBuf", self)
```

分发：

```lua
ProtoDispatcher.DispatchMessage(name, msg)
```

### 5. 当前项目 RPC 模式

当前项目不是 UE RPC，而是协议消息 RPC。

客户端请求：

```lua
NetManager.Send("CGXXXBuf", request)
```

服务器回包：

```lua
ProtoDispatcher.AddDispatch("GCXXXBuf", self, self.OnGCXXXBuf)
```

典型链路：

```text
按钮点击
    ↓
Controller 构造请求 table
    ↓
NetManager.Send("CGXXXBuf", request)
    ↓
TcpClient 发包
    ↓
服务器处理
    ↓
客户端收到 GCXXXBuf
    ↓
ProtoDispatcher 分发
    ↓
Controller 处理回包
    ↓
刷新 Model / View
```

## 十、事件与异步体系

### 1. `Core/EventLoop.lua`

封装 Lua 事件循环。

依赖 C++ 注册的原生模块：

```text
lproject
ltw2.core
ltw2.event
```

提供：

```text
Startup
Shutdown
AddTicker
DelTicker
Timeout
ClockMonotonic
ClockRealtime
SleepFor
```

每帧逻辑：

```text
levent.run
执行 tickerList
```

网络心跳、定时器等都依赖它。

### 2. `Core/Observable.lua`

对象内部观察者模式。

`UIViewBase` 和 `ModelBase` 都依赖它。

适合：

```text
View 内部事件
Model 数据变化事件
```

### 3. `Core/EventDispatcher.lua`

全局事件派发。

适合跨模块事件，例如：

```text
Disconnection
AccountLoginSuccess
ReconnectSuccess
```

### 4. `Core/Delegate.lua` / `Core/MultiDelegate.lua`

基础委托封装，用于单播/多播回调管理。

## 十一、Model 与数据流

### `Core/ModelBase.lua`

Model 基类，内部持有 `Observable`。

提供：

```text
SubscribeEvent
UnsubscribeEvent
TriggerEvent
RemoveObserverEvent
RemoveAllEvent
```

推荐数据流：

```text
网络回包 / 本地逻辑
    ↓
Controller 更新 Model
    ↓
Model.TriggerEvent
    ↓
Controller 或 View 刷新表现
```

## 十二、Utility 工具体系

```text
Script/Utility/
├── LuaHelper.lua
├── Log.lua
├── TableEx.lua
├── Interface.lua
├── JsonFile.lua
├── CsvParser.lua
├── IniParser.lua
├── MsgPackFile.lua
└── XmlParser.lua
```

### 重点文件

#### `LuaHelper.lua`

提供：

```text
XpCall
LuaClass
Handler
HandleFunc
Split
DateFormat
SecondsFormat
DisableGlobalVariable
```

其中：

- `LuaClass` 是普通 Lua 类系统
- `DisableGlobalVariable` 用于禁止非法新增全局变量
- `XpCall` 用于安全调用并打印堆栈

#### `Interface.lua`

用于创建接口式/可调用 Lua 对象。

#### `Log.lua`

日志工具。

#### `TableEx.lua`

table 辅助工具。

#### `JsonFile/CsvParser/IniParser/MsgPackFile/XmlParser`

文件和数据格式解析工具。

## 十三、配置与静态数据

### 1. `Script/Config/Debugger.lua`

读取：

```text
Config/LuaDebugger.ini
```

用于接入 EmmyLua 调试器。

### 2. `Script/Define/CsvDatas.lua`

客户端静态表入口。

当前读取：

```text
Config/DataTable/CardDefine.csv
Config/DataTable/CardLevelUpDefine.csv
Config/DataTable/CardAttributeDefine.csv
Config/DataTable/CardAttrLevelUpDefine.csv
```

并组织为按 id 索引的 table。

## 十四、玩法业务结构

```text
Script/GamePlay/
└── Combat/
    ├── Combat.lua
    ├── GameLogic/
    └── GameView/
```

当前战斗模块大致分为：

```text
Combat 总入口
GameLogic 玩法逻辑
GameView 玩法表现
```

`GM_CombatGameMode.lua` 会调用：

```text
CombatSystem.Initialize
CombatSystem.BeginPlay
CombatSystem.EndPlay
CombatSystem.Deinitialize
```

因此战斗业务主入口建议从：

```text
Script/GamePlay/Combat/GameLogic/CombatSystem.lua
```

开始看。

## 十五、开发与测试目录

### 1. `Script/UI/Develop/`

开发期 UI 和网络测试 UI。

常见文件：

```text
DevelopMainController.lua
WBP_DevelopMain.lua
Network/NetworkController.lua
Network/WBP_Network.lua
```

### 2. `Script/Test/`

测试与示例目录。

包含：

```text
LuaFrameworkTest
UI
UIExample
```

`Test/UIExample` 是学习项目 UI 写法的好参考，里面包含 Main、Activity、Bag、GM、Pet 等示例模块。

## 十六、常见开发链路

### 1. 新增 UI 页面

```text
创建 UE Widget 蓝图
    ↓
创建 Lua View，继承 UI.UIViewBase
    ↓
创建 Lua Controller，继承 UI.UIControllerBase
    ↓
需要数据时创建 Model，继承 Core.ModelBase
    ↓
UIManager.RegisterConfig
    ↓
UIManager.State_Open 或 Dialog_Open
```

### 2. 新增网络 RPC

```text
定义 proto 请求/响应消息
    ↓
配置 message_id.json
    ↓
确保 ProtoDispatcher.ImportProtoFile 导入对应 proto
    ↓
Controller 中 NetManager.Send("CGXXXBuf", request)
    ↓
Controller 中 ProtoDispatcher.AddDispatch("GCXXXBuf", self, self.OnGCXXXBuf)
    ↓
Destroy 时 RemoveDispatch
```

### 3. 新增本地事件

对象内部事件：

```text
Observable / ModelBase / UIViewBase
```

跨模块事件：

```text
EventDispatcher.AddEvent
EventDispatcher.Dispatch
```

### 4. 新增定时逻辑

```lua
EventLoop.Timeout(intervalMs, callback, bLoop)
```

或者：

```lua
EventLoop.AddTicker(obj, func)
```

## 十七、推荐阅读顺序

### 初识项目

```text
1. Source/G01/G01.Build.cs
2. Source/G01/G01.cpp
3. Source/G01/Public/GamePlay/G01GameInstance.h
4. Script/GI_G01GameInstance.lua
5. Script/GameMode/GM_DevelopGameMode.lua
```

### UI 开发

```text
1. Script/UI/UIManager.lua
2. Script/UI/Core/UIConfigSystem.lua
3. Script/UI/Core/UILayerSystem.lua
4. Script/UI/UIViewBase.lua
5. Script/UI/UIControllerBase.lua
6. Script/UI/Develop/Network/NetworkController.lua
```

### 网络开发

```text
1. Script/Net/NetManager.lua
2. Script/Net/TcpClient.lua
3. Script/Net/NetPack.lua
4. Script/Core/Channel.lua
5. Script/Core/ProtoDispatcher.lua
```

### 玩法开发

```text
1. Script/GameMode/GM_CombatGameMode.lua
2. Script/GamePlay/Combat/Combat.lua
3. Script/GamePlay/Combat/GameLogic/CombatSystem.lua
4. Script/GamePlay/Combat/GameView/
```

## 十八、当前结构的关键结论

### 1. C++ 层是能力底座

Source 主要负责：

```text
UE 模块定义
UnLua 环境接入
Lua 原生库注册
GameInstance/GameMode/Player 基类
AssetSubsystem
HTTP/WebSocket/异步加载辅助
```

### 2. Lua 层是业务主体

Script 主要负责：

```text
项目启动
UI 框架
网络协议
事件循环
Model 数据流
玩法逻辑
开发测试工具
```

### 3. 网络不是 UE RPC

当前客户端通信模式是：

```text
NetManager.Send + ProtoDispatcher.DispatchMessage
```

即协议消息 RPC。

### 4. UI 框架已经形成门面 + 层级 + Controller/View/Model 模式

核心是：

```text
UIManager
UIConfigSystem
UILayerSystem
UIViewBase
UIControllerBase
ModelBase
```

### 5. UnLua 与 LuaClass 要分清

```text
UnLua.Class      用于 UE 对象绑定
LuaHelper.LuaClass 用于普通 Lua 逻辑对象
```

## 十九、一句话总结

G01 当前是一个以 UE 为宿主、C++ 提供底层能力、UnLua 承接 UE 生命周期、Lua 承载主要业务逻辑的客户端项目；Lua 层通过 UIManager、NetManager、EventLoop、ProtoDispatcher、ModelBase 等模块组织 UI、网络、事件、数据与玩法，网络通信采用自研协议消息 RPC，而不是 UE DS/Replication 模式。
