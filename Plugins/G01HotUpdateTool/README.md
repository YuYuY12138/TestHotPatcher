# G01HotPatch Commandlet MVP — 使用说明

## 运行命令

```cmd
G:\UE\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe G:\TestProject\TestHotpatch\TestHotpatch.uproject -run=G01HotPatch -config=Plugins/G01HotUpdateTool/Templates/BuildTask_Example.json
```

## BuildTask.json 示例

```json
{
    "taskType": "Patch",
    "platform": "Android",
    "patchType": "Snapshot",
    "baseVersion": "1.0.0",
    "targetVersion": "1.0.1",
    "releaseConfigTemplate": "ReleaseTest.json",
    "patchConfigTemplate": "PatchTest.json",
    "outputDir": "Saved/HotPatcher",
    "options": {
        "bCookPatchAssets": true,
        "bCompressPak": true,
        "bStandaloneMode": false,
        "bCalculateMD5": true,
        "bGenerateManifest": true,
        "bGenerateBuildReport": true
    }
}
```

## 预期输出目录结构

```
Saved/HotPatcher/
├── Releases/
│   └── 1.0.0/
│       └── 1.0.0_Release.json
│
├── Patches/
│   └── 1.0.1/
│       ├── Android/
│       │   └── 1.0.1_Android_001_P.pak
│       ├── VersionManifest_1.0.1.json
│       └── BuildReport_1.0.1.json
│
└── BuildHistory.json
```

## 生成的 VersionManifest 示例

```json
{
    "version": "1.0.1",
    "baseVersion": "1.0.0",
    "patchType": "Snapshot",
    "platform": "Android",
    "buildTime": "2026-06-23T16:00:00Z",
    "files": [
        {
            "name": "1.0.1_Android_001_P.pak",
            "url": "",
            "size": 15728640,
            "md5": "a1b2c3d4e5f60718293a4b5c6d7e8f90"
        }
    ],
    "releaseNote": ""
}
```

## 生成的 BuildReport 示例

```json
{
    "taskType": "Patch",
    "platform": "Android",
    "patchType": "Snapshot",
    "baseVersion": "1.0.0",
    "targetVersion": "1.0.1",
    "buildTime": "2026-06-23T16:00:00Z",
    "duration": 42.5,
    "success": true,
    "outputs": [
        {
            "type": "Pak",
            "path": "1.0.1_Android_001_P.pak",
            "size": 15728640,
            "md5": "a1b2c3d4e5f60718293a4b5c6d7e8f90"
        }
    ],
    "errors": []
}
```

## 生成的 BuildHistory.json 示例

```json
{
    "entries": [
        {
            "targetVersion": "1.0.1",
            "baseVersion": "1.0.0",
            "patchType": "Snapshot",
            "platform": "Android",
            "buildTime": "2026-06-23T16:00:00Z",
            "success": true,
            "reportPath": "Patches/1.0.1/BuildReport_1.0.1.json"
        }
    ]
}
```

## MVP 已支持内容

| 功能 | 状态 |
|------|------|
| Android Snapshot Pak 构建 | ✅ |
| 自动 Export Release | ✅ |
| 自动 Build Patch | ✅ |
| 文件级 MD5 计算（流式 4MB） | ✅ |
| VersionManifest.json 生成 | ✅ |
| BuildReport.json 生成 | ✅ |
| BuildHistory.json 追加 | ✅ |
| Release 已存在时自动跳过 | ✅ |
| BuildTask 参数校验 | ✅ |
| 命令行可独立运行（脱离 Editor UI） | ✅ |
| CI/CD 友好（同一命令行） | ✅ |

## MVP 暂不支持内容

| 功能 | 说明 |
|------|------|
| iOS / Windows 平台 | 后续扩展 GetTargetPlatformEnum |
| Incremental 增量补丁 | 结构已预留，BuildTask.patchType 支持 |
| IoStore (.utoc/.ucas) | 项目 bUseIoStore=False |
| Editor 面板 | Phase 2 |
| CDN 上传 | 后续服务端对接 |
| push_patch 集成 | 后续面板集成 |
| 多 Pak 文件产物 | 当前 Snapshot 只生成一个 Pak |
| Shader/AssetRegistry 热加载 | 客户端侧能力，不在出包工具范围 |
| bNoShaderCompile 默认策略 | 取决于模板 JSON，Commandlet 不覆盖此字段 |
