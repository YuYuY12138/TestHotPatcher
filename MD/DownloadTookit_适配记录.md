# DownloadTookit (ue4-dtkit) 框架分析



---

## 一、整体架构

```
┌─────────────────────────────────────────────┐
│                 调用方（蓝图 / C++）          │
│   RequestDownload → Pause/Resume/Cancel     │
│   HashCheck → GetDownloadProgress/Speed     │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│              UDownloadProxy                  │
│  核心调度类，管理整个下载生命周期             │
│  ┌─────────────────────────────────────┐    │
│  │ 状态机: NotStarted → Downloading    │    │
│  │         → Paused/Canceled           │    │
│  │         → Succeeded/Failed          │    │
│  └─────────────────────────────────────┘    │
│  ┌─────────────────────────────────────┐    │
│  │ 委托: OnDownloadComplete            │    │
│  │       OnDownloadPaused              │    │
│  │       OnDownloadCanceled            │    │
│  │       OnDownloadResumed             │    │
│  └─────────────────────────────────────┘    │
└──────┬──────────────────┬───────────────────┘
       │                  │
       ▼                  ▼
┌──────────────┐  ┌──────────────────┐
│ FMD5Wrapper  │  │  FDownloadFile   │
│ (MD5 计算)   │  │  (文件元数据)     │
│              │  │  - Name          │
│ - Update()   │  │  - URL           │
│ - Final()    │  │  - Size          │
│ - Reset()    │  │  - HASH          │
│ - GetMd5()   │  │  - SavePath      │
└──────────────┘  └──────────────────┘
```

---

## 二、核心类职责

### 1. `UDownloadProxy` — 下载调度器

继承 `UObject`，是整个插件的**唯一核心类**。所有功能都在这个类里。

**状态机**：
```
NotStarted → Downloading → Succeeded
                        → Failed
                        → Paused → Downloading (Resume)
                        → Canceled
```

**关键方法**：

| 方法 | 职责 |
|------|------|
| `RequestDownload` | 入口：发起下载请求，先 HEAD 获取文件大小，再 GET 下载 |
| `Pause` | 暂停：取消当前 HTTP 请求，保留已下载数据 |
| `Resume` | 续传：从已下载位置继续请求剩余内容 |
| `Cancel` | 取消：取消请求、解绑回调、Reset |
| `Reset` | 重置所有状态到初始值 |
| `Tick` | 帧更新（用于计算下载速度 DeltaTime） |
| `HashCheck` | 校验下载文件的 MD5 |

**委托**（蓝图可绑定）：
- `OnDownloadComplete` — 下载完成（成功/失败）
- `OnDownloadPaused` — 暂停
- `OnDownloadCanceled` — 取消
- `OnDownloadResumed` — 恢复

### 2. `FDownloadFile` — 文件元数据

纯数据结构（`USTRUCT`），描述一个下载文件：

| 字段 | 含义 |
|------|------|
| `Name` | 文件名（从 URL 自动解析） |
| `URL` | 下载地址 |
| `Size` | 文件大小（HEAD 请求获取） |
| `HASH` | 下载完成后计算的 MD5 |
| `SavePath` | 本地保存路径 |

### 3. `FMD5Wrapper` — MD5 计算器

使用 UE 内置 `FMD5`（`Misc/SecureHash.h`），边下载边计算 MD5。

- `Update(data, len)` — 每次收到数据块时调用
- `Final()` — 下载完成后调用，返回 MD5 hex 字符串（`const FString&`）
- `GetMd5()` — 获取已计算的 MD5（`const FString&`）
- `Reset()` — 重置计算器

---

## 三、下载流程详解

### 完整流程图

```
RequestDownload(URL, SavePath, bSlice, SliceSize)
    │
    ▼
① HEAD 请求 ──────────────────────────────────
    │  URL + HEAD verb
    │  回调: OnRequestHeadHeaderReceived
    │     → 解析 Content-Length → 文件大小
    │  回调: OnRequestHeadComplete
    │     → 删除旧文件（如果存在）
    │     → PreDownloadRequest()（重置 MD5）
    │     → 构造 Range
    ▼
② GET 请求 ───────────────────────────────────
    │  URL + GET verb + Range header
    │  回调: OnDownloadProcess (每收到数据)
    │     → 写入文件 (Append)
    │     → MD5 Update
    │     → 更新 DownloadedSize / Speed
    │  回调: OnDownloadComplete
    │     → 如果是分片下载且未完成 → 继续下一片
    │     → 否则 → MD5 Final → 广播 OnDownloadComplete
    ▼
③ 完成/暂停/失败
```

### HEAD → GET 两步走

1. **HEAD 请求**：获取 `Content-Length`，确定文件总大小
2. **GET 请求**：带上 `Range: bytes=Begin-End` 头，下载实际内容

### 分片下载

当 `bSlice=true` 且 `SliceByteSize > 0` 时：
- 每次 GET 只请求一个 Range 块（如 `bytes=0-20971519`，即 20MB）
- 当前片下载完成后，`OnDownloadComplete` 中自动请求下一片
- `SliceCount` 记录当前片号

### 暂停/续传

- **暂停**：`HttpRequest->CancelRequest()`，保留 `TotalDownloadedByte`
- **续传**：`Range: bytes=TotalDownloadedByte-文件末尾`
- 文件写入用 `FILEWRITE_Append` 追加模式

### 边下边写 + 边算 MD5

```cpp
// 每收到数据块
FFileHelper::SaveArrayToFile(..., FILEWRITE_Append);  // 追加写入
Md5Proxy.Update(PaddingData, PaddingLength);           // 更新 MD5
TotalDownloadedByte += PaddingLength;                  // 累计大小
```

---

## 四、关键技术细节

### 1. HackHttpResponsePayload

`GetResponseContentData()` 函数通过 `union` 直接读取 HTTP 响应内部的 `Payload` 数组引用，避免 `GetContent()` 的拷贝开销。这是 UE4 时代的 hack，UE 5 中 `GetContent()` 返回 `const TArray<uint8>&`，可以直接用。

### 2. 下载速度计算

```cpp
GetDownloadSpeedKbs() = (DownloadSpeed / 1024) * (1.0 / DeltaTime)
```
`DownloadSpeed` 是当前帧收到的字节数，除以帧间隔得到 KB/s。

### 3. Tick 驱动

`TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(...)` 注册 tick，用于每帧更新 `DeltaTime` 以计算下载速度。

---

## 五、UE 5.7 适配要点

| 问题 | 严重程度 | 说明 |
|------|---------|------|
| Build.cs OpenSSL 路径 | ⚠️ 严重 | 硬编码 `1.1.1k`，UE 5.7 路径可能不同 |
| `SSL` 模块依赖 | ⚠️ 中等 | UE 5.7 可能改名或不需要 |
| `FMD5Wrapper` 用 OpenSSL | 💡 建议 | 可改用 UE 5 内置 `FMD5`，去掉 OpenSSL 依赖 |
| `HackHttpResponsePayload` | 💡 建议 | UE 5 中 `GetContent()` 已返回 `const TArray<uint8>&`，可直接用 |
| `IHttpRequest` API | ⚠️ 中等 | 可能有 API 变更（如 `SetTimeout` 等） |
| `UKismetStringLibrary::Conv_StringToInt` | 💡 建议 | 可改用 `FCString::Atoi` |

---

## 六、UE 5.7 适配改动记录

### 改动 1：DownloadTookit.Build.cs

**删除内容**：
- `LoadOpenSSLLib(Target)` 方法（约 100 行硬编码 OpenSSL 1.1.1k 路径）
- `PublicDependencyModuleNames` 中的 `"SSL"` 模块
- `HACK_HTTP_LOG_GETCONTENT_WARNING=1` 宏
- `PrivateDependencyModuleNames` 中的 `"Slate"` 和 `"SlateCore"` 模块

**原因**：UE 5.7 内置 `FMD5`（`Misc/SecureHash.h`），无需 OpenSSL 依赖；`GetContent()` 已在 UE 5 中返回 `const TArray<uint8>&`，无需 hack；下载核心代码不使用 Slate。

### 改动 2：MD5Wrapper.hpp

**旧实现**：`#include "openssl/md5.h"`，调用 `MD5_Init/Update/Final`，`Final()`/`GetMd5()` 返回 `const char*`
**新实现**：`#include "Misc/SecureHash.h"`，调用 `FMD5::Update/Final`，`Final()`/`GetMd5()` 返回 `const FString&`

**返回类型变更原因**：原实现通过 `TCHAR_TO_ANSI(*Md5String)` 返回 `const char*`，该宏创建栈上临时 `StringCast` 对象，返回的指针在函数返回后悬空。Windows MSVC 未报错，但 Android clang 以 `-Werror,-Wreturn-stack-address` 拒绝编译。改为直接返回 `const FString&` 引用成员变量，彻底消除临时对象问题。

**调用方同步修改**：`DownloadProxy.cpp` 中 `ANSI_TO_TCHAR(Md5Proxy.Final())` 简化为 `Md5Proxy.Final()`（直接赋值 FString，无需 ANSI/TCHAR 转换）。

### 改动 3：DownloadProxy.h

**删除**：`#include "openssl/md5.h"`（第 10 行）
**修改**：`FDelegateHandle TickDelegateHandle` → `FTSTicker::FDelegateHandle TickDelegateHandle`
**修改**：`OnDownloadProcess` 签名 `int32` → `uint64`

### 改动 4：DownloadProxy.cpp

**删除**：`HackHttpResponsePayload` 及相关代码（约 140 行 `#if HACK_HTTP_LOG_GETCONTENT_WARNING` 块）
**简化**：`GetResponseContentData()` 直接调用 `const_cast<TArray<uint8>&>(InHttpResponse->GetContent())`
**删除**：`#if HACK_HTTP_LOG_GETCONTENT_WARNING` 的前向声明行
**修改**：`FTicker::GetCoreTicker()` → `FTSTicker::GetCoreTicker()`（4 处）
**修改**：`OnRequestProgress()` → `OnRequestProgress64()`（2 处绑定）
**修改**：`OnDownloadProcess` 签名 `int32` → `uint64`
**修改**：`ANSI_TO_TCHAR(Md5Proxy.Final())` → `Md5Proxy.Final()`（配合 MD5Wrapper 返回类型变更）
**新增**：`#include "Interfaces/IHttpResponse.h"`

### 改动 5：DownloadProxy.cpp — 构造函数重写（解决 Android 启动崩溃）⚠️ 关键

**原因**：Android 上启动时引擎创建 `UDownloadProxy` 的 CDO（Class Default Object），原构造函数调用 `Reset()` → `Cancel()` → 触发委托广播和 `FTSTicker` 操作。在 CDO 创建阶段引擎尚未完全初始化，这些操作导致 `FMallocBinnedCommon attempted to GetAllocationSizeExternal on an unrecognized pointer` 崩溃。Windows 上不崩溃是因为内存分配器行为不同。

**旧实现**：
```cpp
UDownloadProxy::UDownloadProxy()
    :Super()
{
    Reset();  // 调用 Cancel() → 操作 Ticker/委托 → CDO 阶段不安全
}
```

**新实现**：
```cpp
UDownloadProxy::UDownloadProxy()
    :Super()
    ,Status(EDownloadStatus::NotStarted)
    ,TotalDownloadedByte(0)
    ,LastRequestedTotalByte(0)
    ,DownloadSpeed(0)
    ,DeltaTime(0.f)
    ,bUseSlice(false)
    ,SliceCount(0)
    ,SliceByteSize(0)
{
}
```

用初始化列表直接赋默认值，不调用任何引擎子系统。`Reset()` 仅在运行时由 `Cancel()` 或用户显式调用。

> **⚠️ 这是 Android 平台特有的崩溃。通过 `llvm-addr2line` 解析 NDK 符号确认崩溃点位于 DownloadProxy 代码段。Windows/Editor 下不触发是因为 MSVC 的内存分配器对 CDO 阶段的非法操作更宽容。**

### 改动 6：插件清理

- **删除 `Content/` 目录**：包含 `BP_DownloadGameMode.uasset`、`DownloadProxyMap.umap`、`UI_DownloadProxy.uasset`，均为作者 demo 资源，下载功能不需要
- **`DownloadTookit.uplugin`**：`CanContainContent` 改为 `false`
