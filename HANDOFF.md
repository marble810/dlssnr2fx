# DLSS5-OFX Handoff

> 将 NVIDIA DLSS 5 Neural Rendering（`nvngx_dlssnr.dll`，NGX feature 18）以 OFX 与 After Effects 效果插件接入 Windows 宿主。
>
> **当前架构（覆盖并废止原“阶段一必须走 exe 桥”的冻结）：**
> `OFX/AEX 插件 ⇄ dlssnr2fx.dll ⇄ nvngx.dll_dlssnr.dll ⇄ nvngx_dlssnr.dll`。全部调用在宿主进程内，不再拉起外部 worker 进程。

## 1. 目标与硬红线

输入宿主图像 → packed RGBA8 → 自有引擎 DLL → D3D12 / NGX feature 18 → packed RGBA8 → 宿主图像。参数面板保留 intensity、style、detail、color、structure、preset、HDR、auto mask、adapter 与高光保护。

平台限制：

- Windows 10/11 x64，Visual Studio 2022 C++ Build Tools，C++17。
- NVIDIA RTX 40/50；目标驱动 ≥ 616.56。
- Resolve 必须是 Studio（免费版不支持 OFX）。
- feature 18 是未公开契约；商用前必须解决 NVIDIA 授权/API 路径。

## 2. 架构与生命周期

```text
Resolve / Nuke / Natron / After Effects (宿主进程)
└─ dlssnr.ofx / dlssnr.aex
   ├─ 参数与宿主像素适配
   ├─ pixio: RGBA/ARGB、row stride、8/16/float、highlight protect
   └─ engine_client: 从插件模块同级显式 LoadLibrary(dlssnr2fx.dll)
      └─ dlssnr2fx.dll
         ├─ 每进程、每 adapter 一个串行 Session
         ├─ D3D12 RGBA16F / R32F / RG16F 资源
         ├─ 动态加载驱动 nvngx.dll，取得 capability parameter 与 setter slots
         └─ LoadLibraryExW(nvngx.dll_dlssnr.dll)，经 fwd_* 调 nvngx_dlssnr.dll
```

引擎 C ABI 在 `engine/dlssnr2fx.h`：`nr_eval`、`nr_cancel`、`nr_shutdown`。首次 `nr_eval` 才懒创建 D3D12/NGX，因此 `DLSSNR_PASSTHROUGH=1` 不初始化 NGX。

Session 状态为 Idle / Starting / Ready / Busy / Stopping / Dead：

- 同 adapter 同时最多一个 Evaluate；排队超过 `timeout_ms` 返回 `NR_ERR_BUSY`。
- 尺寸或 `nr_model` 改变时，只在空闲状态 Release/Create feature；detail/color/HDR 不重建 feature。
- `nr_cancel(instance_id, request_id)` 仅 supersede 匹配实例的请求；不能抢占正在驱动中执行的 Evaluate，结果会在完成后丢弃。
- OFX instance destroy 只 cancel 自己的最新请求；OFX unload 与 AE `PF_Cmd_GLOBAL_SETDOWN` 才调用 `nr_shutdown`。
- **禁止从 DllMain 调 `nr_shutdown`**。AE `DllMain` 不日志、不等待、不碰 GPU。

## 3. 颜色与帧契约

插件与引擎边界固定为同尺寸 packed RGBA8。`plugin/pixio.cpp` 负责：

- OFX RGBA 与 AE ARGB 通道映射；
- rowBytes、OFX 纵向翻转、AE top-down；
- OFX 16-bit 65535 与 AE PF_Pixel16 32768；
- 失败透传与高光保护。

引擎上传到 `R16G16B16A16_FLOAT`，dummy depth 为 `R32_FLOAT` 常量 0.5，dummy motion 为 `R16G16_FLOAT` 全零，输出为 RGBA16F。CPU 合成为 `lerp(orig, nr, detail)`；color 控制 NR 色度，HDR 开关使用简单线性域混合。

Motion UI 为兼容既有项目继续显示，但本版本不传 motion/NVOF/LK；真 motion 与 SR 都在范围外。

## 4. Runtime 发现与部署

合法运行时目录必须同时包含 **`nvngx.dll_dlssnr.dll`** 与 **`nvngx_dlssnr.dll`**：

1. `DLSSNR_RUNTIME_DIR`；
2. 从插件模块目录向上查找每级的 `runtime\`；
3. 或插件同级目录。

`nvngx.dll_dlssnr.dll` 是很薄的进程内 ABI 适配层：它只导出 `fwd_*`，不是 exe、worker 或管道。引擎另外动态加载驱动侧 `nvngx.dll` 来取得 capability parameter 块及 setter vtable slots，再把它们交给 forwarder。缺任一运行时 DLL 都返回 `NR_ERR_MISSING_RUNTIME`，插件记录日志并逐帧透传。

最小安装清单：

- OFX：`Contents\Win64\dlssnr.ofx` 与同级 `dlssnr2fx.dll`；forwarder + snippet 放 bundle 的 `runtime\` 或通过环境变量指定。
- AEX：`dlssnr.aex` 与同级 `dlssnr2fx.dll`；forwarder + snippet 放相邻/上级 `runtime\` 或通过环境变量指定。

不再需要外部 worker 进程、`nvngx_dlss.dll` 或任何 `nvsdk_ngx*.lib`。

## 5. 构建

```bat
build.bat help
build.bat engine
build.bat selftest
build.bat plugin
build.bat aex
```

`build.bat help` 的目标列表为 `engine | plugin | aex | selftest`：

- `engine` → `dist\engine\dlssnr2fx.dll`；链接仅 `d3d12.lib dxgi.lib ole32.lib`。
- `plugin` → `dist\dlssnr.ofx.bundle\Contents\Win64\dlssnr.ofx`，并复制引擎 DLL 到同级。
- `aex` → `dist\aex\dlssnr.aex`，并复制引擎 DLL 到同级。默认 AE SDK 为仓库内 25.6 Examples，可用 `AE_SDK_DIR` 覆盖。
- `selftest` → C/C++ ABI include 检查、缺 runtime/cancel/shutdown/session-policy/composite 检查与 pixio 检查。设置 `DLSSNR2FX_RUN_GPU=1` 才运行实机 NGX round-trip。

构建仍使用 staging 二进制（`dist\dlssnr_build.ofx` / `build\aex\dlssnr_build.aex`），避免已打开宿主锁住最终产物。

## 6. OFX 验收

开发可用 Natron；发布在 Resolve Studio / Nuke 验收：

- describe/load 在引擎 DLL 或 runtime 缺失时仍成功；
- 一帧输出非零且与输入不同；改变 intensity/style 后输出变化；
- `DLSSNR_PASSTHROUGH=1` 输出逐字节等于输入，日志没有 NGX Init；
- 两个节点连续/并发渲染共享 adapter session，不互相终止；
- 缺 runtime、BUSY、TIMEOUT、SUPERSEDED、NGX/GPU 失败全部透传；
- unload 日志后引擎释放。

日志：插件 `%TEMP%\dlssnr-ofx.log`；引擎 `%TEMP%\dlssnr2fx.log`。

## 7. After Effects 插件（AE 2025 / 25.6）

`plugin/aex/dlssnr_aex.cpp` 使用经典 PF CPU 整帧路径，保留 21 项参数、PiPL 注册与 8/16bpc 支持，不声明 `PIX_INDEPENDENT` 或 `FLOAT_COLOR_AWARE`。PiPL 流程：`.r` → `PiPLtool.exe` → `.rc` → `rc.exe` → link。

安装到：

- `C:\Program Files\Adobe\Adobe After Effects 2025\Support Files\Plug-ins\`；或
- 将该目录下开发 junction 指向 `dist\aex`。

实机验收同 OFX，并确认 `PF_Cmd_GLOBAL_SETDOWN` 后释放；`DllMain(PROCESS_DETACH)` 不得出现等待、日志或 GPU 调用。AE effect identity 用 `PF_InData::effect_ref` 派生为 request instance id。

## 8. 风险与取舍

1. **进程内故障面：** NGX/TDR 现在可能拖垮宿主。这是移除 exe 隔离的明确取舍；C ABI 捕获 C++ 异常、失败进入 Dead 并约 1 秒退避。
2. **Evaluate 不可抢占：** timeout 能约束等锁、创建、提交与正常 fence 等待，但无法保证中断已卡在驱动内部的调用。cancel 只能丢弃返回结果。
3. **未公开 ABI：** feature 18、`DLSSNR.*`、forwarder `fwd_*` 与 capability vtable slots 都可能随运行时/驱动变化；slots 每次动态探测，失败返回 `NR_ERR_NGX`。forwarder 无 shutdown 导出，因此释放 feature/capability/GPU 后保留驱动 loader 到进程退出，避免已验证的卸载崩溃。
4. **多宿主：** 每个进程各有自己的 session；AE 与 Resolve 不共享跨进程锁。
5. **授权：** 当前运行时仅适合研究/自用验证。

## 9. 明确不做

- fork 第三方 NR CLI / stdin 协议 / Python / Comfy 工具链；
- 官方 DLSS SDK import library、Streamline、RTX Neural Rendering SDK；
- DLSS SR、真 depth、NVOF/Lucas-Kanade motion、32bpc native/AE Smart Render；
- sidecar 进程、snippet 直调回退、自写第二份 forwarder、多 GPU 负载均衡、macOS/Linux。
