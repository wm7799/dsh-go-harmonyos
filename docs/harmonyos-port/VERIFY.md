# DSH-Go 1.3.0 鸿蒙/卓易通修复移植 —— 主 Agent 实测核验报告

核验人：主 Agent（Hermes）
核验时间：2026-11-22
产物来源：dev-02（pi 运行时）经 LobeHub 派发产出，中继 8898 回传
产物文件：dsh-dev02-final.tgz（19478 B，md5 9263e7c44e9e78364a1a4b0285bb7622）

## 一、移植内容

把 QQZping v.2（基于 dsh-Go 1.0.16 的鸿蒙/卓易通修复）的增量修复，移植到上游 ccch1mneyyy/dsh-Go 1.3.0（HEAD 3c8637d）。

上游 1.3.0 已自带 DirectLaunch / Tawcroot 双引擎，因此 DirectLaunch.kt、SafeMode.kt 无需改动（实测两者 upstream130 与 patched 版本逐字节相同）。真正需要移植的只有两个文件：

- `fixed-DshProcess.kt`：6 组改动 / 9 个 hunk
- `fixed-CrashClassifier.kt`：2 个判别式

另附 `noclone3_launcher.c`（clone3→ENOSYS 的 seccomp 前置 helper）及接入说明。

## 二、实测核验项（全部基于包内原始对照文件，非自述）

| # | 核验项 | 方法 | 结果 |
|---|---|---|---|
| 1 | DshProcess 改动幅度 | `diff upstream130 → fixed` 数 hunk | **9 hunk**，与 patched 的 9 hunk 一一对应 ✓ |
| 2 | 与 patched 的差异 | `diff patched → fixed` 数 hunk | 仅 **1 hunk**，且为删除一段重复 KDoc 注释，无功能影响 ✓ |
| 3 | 1.3.0 特有分支保留 | 关键字计数 | tawcPlan=5 / TawcrootLaunch=8 / untangleTawcroot=2 / TawcrootLinks=5 / DirectLaunch=16 / prootArguments=2 / prootNodeOptions=1 / tidyLinks=2，全部逐字保留 ✓ |
| 4 | CrashClassifier 改动幅度 | `diff upstream130 → fixed` 数 hunk | **1 hunk**（新增两个判别式）✓ |
| 5 | 1.3.0 特有字段保留 | 关键字计数 | prootCompatibilityFailure=2 / isProotCompatibilityFailure=1 / EXEC_FAILED=6，均保留（未跟随 patched 的错误删除）✓ |
| 6 | 括号配平 | 字符计数 | fixed-DshProcess braces 0 / parens 0；fixed-CrashClassifier braces 0 / parens -1 |
| 7 | parens -1 归因 | 基线对比 | upstream130 与 patched 的 CrashClassifier 也是 parens -1（字符串常量内括号），fixed 未引入新问题 ✓ |
| 8 | SafeMode 无需移植 | `cmp upstream130 vs patched` | IDENTICAL，确认无需改动 ✓ |
| 9 | start() 调用点参数错位风险 | 逐位置核对 RuntimeController.kt:647 | 位置实参仅到 speechOverlay（第 8 个）；其后 tawcEngine/tawcFailed 均为**命名实参**、尾随 lambda 对应 onLaunchMethod；noSeccomp 有默认值被跳过 → **不会错位** ✓ |

## 三、核验结论

产物是**真实的增量移植**，不是整文件覆盖，1.3.0 的新引擎逻辑完整保留。PORT-NOTES.md 记录的落点与实测一致。唯一被标注为「中风险」的 start() 参数顺序问题，在仓库唯一调用点上因使用命名实参而不触发。

**未做**：APK 构建（按任务书要求，不在本次范围）。noclone3_launcher.so 的编译接入、useLegacyPackaging、manifest 索引等集成步骤已在 PORT-NOTES 第三节给出可直接执行的方案，需在构建 APK 时落实。

## 四、体系层修复记录（本轮顺带修掉）

1. 回传中继 sr-relay（8898）用单线程 HTTPServer，被一个僵死连接堵死（LISTEN 但 accept 不动），导致设备回传全部失败 → 已改 ThreadingHTTPServer 并重启，公网恢复 200。
2. dev-01（codex 运行时）连续两次 `tools[4].type:type is illegal`，dev-03（claude + glm-5.3-flash）零输出 —— 属运行时/模型兼容问题，本轮改由稳定的 dev-02（pi）单台完成全部移植并已验收。
