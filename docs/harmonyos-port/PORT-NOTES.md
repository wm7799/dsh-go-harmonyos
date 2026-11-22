# 鸿蒙卓易通修复增量：1.0.16（ZhengP 修改版）→ 上游 1.3.0 移植说明

对象文件：

- `fixed-DshProcess.kt` —— 以 `upstream130-DshProcess.kt` 为底，融入 `patched-DshProcess.kt` 的 6 组改动（9 个 hunk）
- `fixed-CrashClassifier.kt` —— 以 `upstream130-CrashClassifier.kt` 为底，加入 `isProotSeccompFailure` / `isProotCwdFailure`

原则：**只做增量**，1.3.0 已有的 tawcroot / Direct 双引擎逻辑原样保留；不做整文件覆盖。
校验：`diff upstream130-DshProcess.kt fixed-DshProcess.kt` = 9 个 hunk；`diff patched-DshProcess.kt fixed-DshProcess.kt` 只剩 1 处（patched 里重复的一段陈旧 KDoc 未搬入）。

---

## 一、DshProcess.kt 逐条落点

| # | 改动点（1.0.16 patched） | 在 1.3.0 的落点 | 与 tawcroot / Direct 的冲突处理 | 风险 |
|---|---|---|---|---|
| 1 | `noSeccomp` 字段（`@Volatile var`，private set）+ `prootLaunch` 属性（`!direct && !tawc`） | 类字段区，紧跟 `tawc` 之后、`launchDiagnostics` 之前（原文件 74 行附近） | `prootLaunch` 用**现有** `direct`/`tawc` 两个既有标志推导，不新增状态机；tawcroot 命中时 `tawc=true` → `prootLaunch=false`，所有 PRoot 专属补丁自动跳过；Direct 命中同理 | 低。纯派生属性，与 1.3.0 的 `launchDiagnostics.selected(...)` 无交叉 |
| 2 | `start()` 增加 `noSeccomp` 形参并 `this.noSeccomp = noSeccomp` | 签名插在 `speechOverlay` 与 `tawcEngine` **之间**（与 patched 一致）；赋值紧跟 `check(process == null)` | 参数顺序与 1.0.16 patched 完全相同，1.3.0 新增的 `tawcEngine`/`tawcFailed` 仍在其后 | **中**。位置参数调用方会错位。仓库内调用点必须全部用命名实参（`noSeccomp = ...`）；若发现位置调用，改为追加到签名末尾更安全 |
| 3 | `prootNodeCommand()`：绕过 `/usr/local/bin/dsh` shim，直启 `/opt/node/bin/node` | 新增私有方法，放在 `tidyLinks()` 之前（1.3.0 的 `tidyLinks` 前正好是 start() 结束处） | 仅在 `if (prootLaunch)` 分支调用，改写 `builder.command()` 的**最后一个**元素。tawcroot 分支已用 `tawcPlan.argv` 覆盖 command、Direct 分支已用 `plan.argv` 覆盖 command，二者都在此块之前完成，且 `prootLaunch` 为 false，不会被改写 | 中。依赖 r15 rootfs 布局（`opt/node/bin/node` + `opt/dsh/node_modules/@deepseek-ai/dsh/lib/bin.js`），两者缺失时原样返回 shim 命令，属安全降级；`--import=/opt/dsh-go/boot-cache.mjs` 只对 `web` 首参注入，安全模式（`--profile ... web`）不注入，与 1.3.0 的 `SafeMode.profileArg` 行为一致 |
| 4 | `installNoClone3Launcher()`：把 `libnoclone3_launcher.so` 前置到 argv[0] | 新增私有方法，紧随 `prootNodeCommand()` | 同样只在 `prootLaunch` 分支生效；helper 只负责装 seccomp 规则再 `execv` PRoot，**不改变** PRoot/tawcroot 的引擎选择与 argv 语义（helper 之后仍是原 PRoot 命令） | 中。`.so` 不存在或不可执行时打日志并跳过，不阻断启动；要求 nativeLibraryDir 里的该文件是**可执行 PIE**（见第三节），若 APK 用 `useLegacyPackaging=false` 则不会被解出，必须开启 legacy packaging |
| 5 | `PROOT_NO_SECCOMP=1` 注入 + launch method 日志区分 `proot (seccomp off)` | ① 注入点在 tawcPlan 分支之后、`readyReported = false` 之前，条件 `if (noSeccomp && prootLaunch)`；② 日志由三目表达式改为 `when { tawc / direct / noSeccomp / else }` | 注入写在 **host 侧 `env`**（`/usr/bin/env -i` 之后会被清空，写进 guestEnv 无效）；`tawc`/`direct` 分支已 `env.clear()` 并换成 plan.environment，此处条件含 `prootLaunch`，不会污染 tawcroot/Direct 环境 | 低。`PROOT_NO_SECCOMP` 只被 PRoot 读取，tawcroot/Direct 不读；`(safe recovery)` 后缀逻辑原样保留 |
| 6 | `reapStrays` 放宽判断 + `pidHint()` | ① 新增 `pidHint()`（读 `rootfs/tmp/dsh-web.pid`，异常返回 null），放在 `processSnapshot()` 与 `remember()` 之间；② `reapStraysLocked()` 内 `val hinted = pidHint()?...` 并改为 `processSnapshot(cleanupKnown.keys + hinted)`；③ KDoc 措辞改为 "The default-mode startup policy may continue..." | 关键点：`start()` 中 `reapStrays(allowObservationGap = true)` 改为 `allowObservationGap = !selection.direct && !tidy`，即**只有纯 PRoot 直启才允许观察缺口**；Direct/tawcroot（含 tidy 迁移路径）仍要求完整观察。紧随其后的 `check(!cleanup.ownedRemaining)` 改为 `check(cleanup.complete \|\| (!direct && !tawc && !cleanup.ownedRemaining))`，同样放行范围只限 `!direct && !tawc` | **中**。1.3.0 注释原本明确禁止用持久化裸 PID 补充枚举（PID 复用风险）；`pidHint()` 重新引入该 PID，仅作为 `known` 提示参与 `RuntimeProcessOwnership.scan`，真正判定仍走 `owned(...)` 的 uid+starttime+rootfs 校验，误杀面被限制在同 uid 同 rootfs；`killStrays()` 仍以 `allowObservationGap=false` 调用，退出路径的严格性不变。另：pid 文件在 `reapStrays` 之后才删除，`pidHint()` 读到的仍是上一轮残留值（这是设计意图） |

补充：1.3.0 中 `tawc`/`direct` 在 `check(...)` 之前已赋值，因此第 6 条的 `check` 放宽条件可直接引用；`cleanup.canPruneTemp` 在观察不完整时为 false，proot-tmp 不会被提前清理，行为安全。

---

## 二、CrashClassifier.kt 逐条落点

| # | 改动点 | 在 1.3.0 的落点 | 冲突处理 | 风险 |
|---|---|---|---|---|
| 7 | `isProotSeccompFailure(exitCode, tail)`：126 + `getcwd() failed: Function not implemented` + `EXEC_FAILED` 命中 | `isFatal()` 之后、`redact()` 之前新增 | 复用 1.3.0 已有的私有 `EXEC_FAILED` 正则（含 `Bad address`/`Function not implemented` 等），不新增正则 | 低。签名刻意收窄（必须 126 且两条同时命中），插件报错或运行时缺失不会误判 |
| 8 | `isProotCwdFailure(exitCode, tail)`：1/126/182 + `getcwd() ...` / `process.cwd failed` / `uv_cwd` / `ENOSYS: process.cwd` | 紧随 #7 | 与 1.3.0 的 `RunFailure.prootCompatibilityFailure` 字段、`isProotCompatibilityFailure(failure, tail)` **并存保留**（1.3.0 新增，1.0.16 没有；SafeMode.kt 两版完全一致，无调用点冲突） | 低。182 = 128+SIGPOLL 之外的 Node 侧退出码选择较宽，靠字符串特征收敛；调用方需注意它与 `isProotSeccompFailure` 有重叠（前者宽、后者窄），应先窄后宽判断 |

未移植（有意）：`RunFailure.prootCompatibilityFailure` 字段与 `ENV_SHELL_EXEC_FAILED` 分支在 1.3.0 已存在，patched 版本是删掉它们的旧基线，**不跟随删除**。

---

## 三、`noclone3_launcher.c` 如何接入 `runtime/build-runtime.sh`

现状：`build_abi()` 里 `jni="$out/jniLibs/$abi"`，只从 Termux deb 拷 4 个库（`libproot.so`、`libproot-loader.so`、`libtalloc.so`、`libandroid-shmem.so`），随后 `patch-dynstr` 改 SONAME、`chmod 0755`、`elf-info` 出 `jnilibs.json`，最后在 `-- verification` 段用 `file -b` 校验每个 `$jni/*.so` 的 ELF machine。接入需要 5 处改动，全部在 `build_abi()` 内：

1. **编译（在 4 个 `cp` 之后、`chmod` 之前）**：用 NDK clang 交叉编译成 **PIE 可执行文件**（不是 shared object），文件名必须 `lib*.so` 才能进 jniLibs：

   ```
   ndk_clang="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
   case "$abi" in arm64-v8a) cc_triple=aarch64-linux-android;; x86_64) cc_triple=x86_64-linux-android;; esac
   "$ndk_clang/${cc_triple}21-clang" -O2 -fPIE -pie -fno-asynchronous-unwind-tables \
     -Wl,--build-id=none -o "$jni/libnoclone3_launcher.so" "$HERE/native/noclone3_launcher.c"
   ```

   `-fPIE -pie` 是硬要求（Android 10+ 禁止非 PIE 可执行文件）；`SECCOMP_RET_ERRNO|ENOSYS` 规则与 `PR_SET_NO_NEW_PRIVS` 在容器内核上可用，`prctl` 失败时源码会回退 `syscall(SYS_seccomp, ...)`，两者都失败则返回 126 并在 stderr 说明——`DshProcess` 侧不会因此崩，只是没装过滤器。

2. **打包**：产物直接落在 `$jni`，因此天然进入 APK 的 `jniLibs/<abi>/`；`app/android/app/build.gradle.kts` 的对应 flavor 必须保持 `jniLibs.useLegacyPackaging = true`（`android:extractNativeLibs="true"`），否则 `.so` 留在 APK 内不会解到 `applicationInfo.nativeLibraryDir`，`installNoClone3Launcher()` 会打印 "noclone3 launcher missing" 并降级。

3. **清单/哈希**：在 `python3 -I - "$jni" ... <<PYEOF` 的 `src` 字典里追加一项，让 `manifest.json`/`jnilibs.json` 带上它（`patch=None`、`orig=None`，`inner` 写 `native/noclone3_launcher.c`），否则 `write_index` 的 jniLibs 索引里查不到、升级校验会对不上。

4. **ABI 校验**：无需新代码，现有 `for f in "$jni"/*.so; do file -b "$f" | grep -q "$elf_name"` 循环自动覆盖新文件（`file` 对 PIE 可执行同样输出 aarch64/x86-64）。

5. **`runtimeVersion` 失效**：把 `sha256 "$HERE/native/noclone3_launcher.c"` 加进 `inputs_sha` 的 `sha256sum` 管道，源码或编译参数一改就会换 runtimeVersion，避免旧 manifest 复用。

注意：不要给这个文件做 `patch-dynstr`（它不 `DT_NEEDED` talloc），也不要 `chmod 0755` 之后再 strip（`--build-id=none` 已避免不可复现的 build-id）。x86_64 ABI 同样编译，便于模拟器/宿主自测。

---

## 四、验收清单

- [x] `diff upstream130-DshProcess.kt fixed-DshProcess.kt` → 9 hunk，与 patched 的 9 hunk 一一对应
- [x] 1.3.0 的 tawcroot 分支（`tawcPlan`、`TawcrootLaunch.writeDevView`、`untangleTawcroot`、`TawcrootLinks.pending`）逐字保留
- [x] 1.3.0 的 Direct 分支（`DirectLaunch.prepare/select/prootArguments/prootNodeOptions`、`tidyLinks`）逐字保留
- [x] 括号配平：`fixed-DshProcess.kt` braces 0 / parens 0；`fixed-CrashClassifier.kt` braces 0（parens 差值与 upstream130 基线相同，来自字符串常量）
- [x] `upstream130-SafeMode.kt` 与 `patched-SafeMode.kt` 完全相同 → SafeMode.kt 无需移植（安全模式直启 Node 由 `prootLaunch` 分支天然覆盖）
- [ ] 未做：APK 构建（按任务要求不构建）
