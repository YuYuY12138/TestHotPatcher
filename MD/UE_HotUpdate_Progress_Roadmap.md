# UE 热更新推进事项清单

> 技术栈：UE 5.7 + UnLua + HotPatcher + DownloadToolkit（DTKit）  
> 排序原则：先确认底层形态，再验证热更能力，然后搭建完整客户端链路，最后补稳定性、真机和发布自动化。

---

## 一、完整推进事项

| 优先级 | 推进事项 | 具体内容 |
|---:|---|---|
| 1 | **确认项目打包模式** | 确认 `Use Pak File`、`Use Io Store`、`Generate Chunks`、Pak Signing、Encryption；明确后续走纯 Pak 还是 IoStore 文件组方案。 |
| 2 | **确认实际挂载接口** | 确认项目使用 `FPakPlatformFile`、HotPatcher Runtime、IoDispatcher 或自研接口；验证 MountOrder、补丁覆盖顺序和运行时挂载能力。 |
| 3 | **Lua 脚本热更验证** | 将 `Script/` 下 Lua 文件打入补丁；挂载后首次 `require` 验证加载新版本；增加版本日志；明确已加载 Lua 模块不会自动刷新。 |
| 4 | **多类型 UE 资产热更测试** | 验证 Texture、Material、Map、Blueprint、WidgetBlueprint、DataTable、DataAsset 等；区分挂载前已加载和未加载两类情况。 |
| 5 | **Shader 热更验证** | 验证 Material 修改后是否需要显式加载 Shader Library；重点检查 Android 真机是否出现黑材质、默认材质。 |
| 6 | **AssetRegistry / AssetManager 验证** | 验证新增资源、SoftObjectPath、PrimaryAsset、GameFeature 是否能被发现；确认是否需要追加 Registry 或 ScanPaths。 |
| 7 | **启动后 MountPak / IoStore 测试** | 游戏启动后扫描本地补丁目录；按版本和优先级挂载；验证补丁覆盖基础包；验证挂载失败分支。 |
| 8 | **Bootstrap 启动关卡设计** | 新建极简 `L_Bootstrap`；设置为默认启动地图；只承载更新 UI 和热更逻辑；更新完成后切换正式登录关卡。 |
| 9 | **远端 Manifest 结构设计** | 定义基础版本、目标版本、最低基础包版本、文件列表、URL、Size、MD5、MountOrder、强制更新、重启要求等字段。 |
| 10 | **本地版本文件设计** | 记录基础版本、本地补丁版本、已安装补丁、文件路径、MD5、MountOrder、安装状态和时间。 |
| 11 | **热更新 Subsystem 设计** | 实现 `UHotUpdateSubsystem : UGameInstanceSubsystem`；负责版本检查、下载调度、校验、安装、挂载、恢复、回滚和版本提交。 |
| 12 | **热更新状态机设计** | 定义 Initializing、RequestingManifest、ComparingVersion、Downloading、Verifying、Installing、Mounting、Completed、Failed 等状态。 |
| 13 | **版本检查 + 补丁下载流程** | 请求 Manifest；比较本地和服务端版本；生成下载列表；检查网络、空间；调用 DTKit 下载到 Pending。 |
| 14 | **DTKit MD5 流程确认** | 验证插件是否边下载边 `MD5 Update`；确认下载完成后 `Final`；检查 Hash 与 Manifest 预期值的比较位置。 |
| 15 | **断点续传 MD5 验证** | 验证 Pause / Resume 是否保留 MD5 上下文；进程重启后如何补算已有文件；必要时增加完整文件复核。 |
| 16 | **下载目录规范** | 设计 `Manifest / Pending / Installed / Backup`；下载文件使用 `.download` 临时后缀；禁止直接写入正式目录。 |
| 17 | **安装事务设计** | 下载、校验、安装、挂载全部成功后才提交新版本；任何步骤失败都不能修改本地有效版本。 |
| 18 | **下载状态维护** | 支持暂停、继续、取消、失败重试、中途退出、网络变化、磁盘空间检查、任务持久化。 |
| 19 | **更新 UI** | 显示检查版本、更新大小、下载进度、速度、剩余大小、校验、安装和挂载状态；提供重试、暂停、继续、退出。 |
| 20 | **未完成任务恢复** | 启动时扫描 Pending；恢复上次下载、重新校验、继续安装，或清理过期任务。 |
| 21 | **失败回滚设计** | MD5、安装、挂载、Shader、Registry 任一步失败时保留旧版本；不提交新版本；支持重新下载或降级。 |
| 22 | **补丁撤回与黑名单** | 支持服务端禁用错误补丁；客户端识别黑名单 Patch；必要时回退到上一有效版本。 |
| 23 | **后台下载设计** | 第一阶段先支持 App 前后台暂停/恢复；第二阶段再考虑登录后后台预下载和安全时机挂载。 |
| 24 | **日志与观测系统** | 记录状态切换、版本号、文件名、Size、MD5、耗时、MountOrder、错误码；统一 `LogHotUpdate` 等日志前缀。 |
| 25 | **热更新调试面板** | 开发环境查看本地版本、远端版本、Pending 文件、已安装补丁、已挂载 Pak、当前状态和错误信息。 |
| 26 | **自动化异常测试** | 覆盖无更新、MD5 错误、断网、弱网、磁盘不足、下载中杀进程、挂载失败、多补丁覆盖等场景。 |
| 27 | **迁移到 G01 实际测试** | Demo 跑通后接入 G01 的 GameInstance、启动状态机、登录流程、目录和配置；记录项目差异。 |
| 28 | **Android 真机完整验证** | 验证沙盒路径、前后台、断点续传、运行时挂载、Shader、登录关卡切换和性能。 |
| 29 | **iOS 真机验证** | 验证 Documents / Saved 路径、签名限制、挂载行为、后台生命周期和补丁更新。 |
| 30 | **CI/CD 自动出补丁** | Commandlet 调用 HotPatcher；自动输入基础版本、目标版本和平台；生成补丁、Manifest、Size、MD5。 |
| 31 | **自动上传与发布** | 上传 CDN / OSS；更新 Latest Manifest；保留构建产物、日志和历史版本；失败时阻止发布。 |
| 32 | **灰度发布与监控** | 按渠道、账号或比例灰度下发；监控下载成功率、MD5 失败率、挂载失败率和启动耗时。 |

---

## 二、阶段划分

### 阶段一：验证底层能力

对应事项：

```text
1～7
```

目标：

```text
确认 Pak / IoStore
→ 确认挂载接口
→ Lua 可更新
→ UE 资产可更新
→ Shader 与资源发现可用
→ 补丁可以正确挂载
```

### 阶段二：搭建完整客户端链路

对应事项：

```text
8～19
```

目标：

```text
Bootstrap
→ Manifest
→ Subsystem
→ 状态机
→ DTKit 下载
→ MD5
→ 安装
→ 挂载
→ 更新 UI
```

### 阶段三：补充稳定性

对应事项：

```text
20～26
```

目标：

```text
恢复
→ 回滚
→ 撤回
→ 后台
→ 日志
→ 调试
→ 异常测试
```

### 阶段四：项目化与发布

对应事项：

```text
27～32
```

目标：

```text
迁移 G01
→ Android / iOS 真机
→ CI/CD
→ 自动上传
→ 灰度发布
→ 数据监控
```

---

## 三、当前最高优先级

```text
1. 确认 Pak / IoStore 打包模式
2. 确认项目真实挂载接口
3. 验证 Lua 与多类型 UE 资源热更
4. 验证 DTKit 边下载边计算 MD5
5. 验证断点续传后的 MD5 连续性
6. 建立 Bootstrap 启动关卡
7. 确定 Manifest 数据结构
```

---

## 四、关键原则

```text
HotPatcher
→ 负责差异分析、Cook 和补丁构建

DTKit
→ 负责下载、断点续传、进度和 MD5

UHotUpdateSubsystem
→ 负责版本检查、调度、校验、安装、挂载、恢复与提交

Bootstrap
→ 承载启动阶段热更

UnLua
→ 补丁挂载完成后再初始化业务

本地版本
→ 下载、校验、安装、挂载全部成功后才提交

失败策略
→ 新补丁失败时，旧版本始终保持可运行
```

---

## 五、进度状态建议

每项可增加：

```text
状态：未开始 / 进行中 / 待验证 / 已完成 / 阻塞 / 已取消
负责人
计划开始时间
计划完成时间
实际完成时间
风险
验证结果
关联文档
```
