#pragma once

#include "CoreMinimal.h"

/**
 * FG01HotPatcherAdapter - HotPatcher 封装层
 *
 * 职责：
 *   隔离 G01 构建工具与 HotPatcher 底层 API 的直接耦合。
 *   对外提供 ExportRelease / BuildPatch 两个稳定接口。
 *   内部负责：加载配置模板 → 填充业务参数 → 调用 HotPatcher Proxy → 收集产物路径。
 *
 * 设计约束：
 *   - G01HotPatchCommandlet 只通过 Adapter 访问 HotPatcher，不直接依赖 Proxy 类
 *   - MVP 阶段 Adapter 内部直接调用 UReleaseProxy / UPatcherProxy（同进程）
 *   - 后续如需切换为子进程模式，只改 Adapter 内部实现，上层不变
 *
 * 线程安全：
 *   不保证线程安全，必须在 GameThread 调用。
 */
class G01HOTUPDATETOOL_API FG01HotPatcherAdapter
{
public:

    /**
     * 导出 Release（基础版本快照）
     *
     * 执行步骤：
     *   1. 加载 TemplatePath 指定的 Release 配置模板 JSON
     *   2. 覆盖 VersionId、SavePath、Platform 等字段
     *   3. 创建 UReleaseProxy → Init → DoExport
     *   4. 成功后产物为 {OutputDir}/{Version}_Release.json
     *
     * @param Version       版本号（如 "1.0.0"）
     * @param Platform      目标平台（如 "Android"）
     * @param TemplatePath  Release 配置模板 JSON 绝对路径
     * @param OutputDir     产物输出目录绝对路径
     * @param OutError      [out] 失败时的错误描述
     * @return              true = 成功
     */
    static bool ExportRelease(
        const FString& Version,
        const FString& Platform,
        const FString& TemplatePath,
        const FString& OutputDir,
        FString& OutError
    );

    /**
     * 构建 Patch（补丁包）
     *
     * 执行步骤：
     *   1. 加载 TemplatePath 指定的 Patch 配置模板 JSON
     *   2. 覆盖 VersionId、BaseVersion、PakTargetPlatforms、SavePath 等字段
     *   3. 创建 UPatcherProxy → Init → DoExport
     *   4. 扫描输出目录中的 *.pak 文件作为产物
     *
     * @param BaseVersion       基准版本号（如 "1.0.0"）
     * @param TargetVersion     目标版本号（如 "1.0.6"）
     * @param Platform          目标平台
     * @param TemplatePath      Patch 配置模板 JSON 绝对路径
     * @param BaseReleaseJson   基准版本的 Release JSON 文件绝对路径
     * @param OutputDir         产物输出目录绝对路径
     * @param bCookAssets       是否 Cook 资产
     * @param bCompress         是否压缩 Pak
     * @param OutPakPaths       [out] 生成的 pak 文件绝对路径列表
     * @param OutError          [out] 失败时的错误描述
     * @return                  true = 成功
     */
    static bool BuildPatch(
        const FString& BaseVersion,
        const FString& TargetVersion,
        const FString& Platform,
        const FString& TemplatePath,
        const FString& BaseReleaseJson,
        const FString& OutputDir,
        bool bCookAssets,
        bool bCompress,
        TArray<FString>& OutPakPaths,
        FString& OutError
    );

private:

    /**
     * 将平台字符串转换为 HotPatcher 的 ETargetPlatform 枚举值
     * 当前 MVP 只处理 "Android" → AndroidClient 的映射
     */
    static bool GetTargetPlatformEnum(const FString& PlatformName, int32& OutEnumValue);

    /**
     * 解析路径中的 [PROJECTDIR] 变量替换为实际项目路径
     */
    static FString ResolvePath(const FString& InPath);
};
