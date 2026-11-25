# 显示器控制（MonitorOSD）

**用软件直接控制显示器的画面参数，并提供「基准保存 / 一键还原 / 自动还原」。**

> A free Windows tool that controls monitor picture settings over DDC/CI, with profile
> save & restore — built for internet cafés where every customer leaves the monitor
> in a different state.

支持卓威（ZOWIE/BenQ）、华硕（ASUS ROG/TUF）、AOC/飞利浦等主流电竞显示器，
通过 Windows 标准 **DDC/CI** 协议直控显示器，不装驱动、不动显卡设置。

- 单个 exe（约 1 MB，C++ /MT 静态链接），**Win10/11 免安装、免运行库**
- 全中文界面，标签页分栏、滑条/下拉框即调即生效
- 支持多显示器、换屏自动重识别、托盘常驻、全局热键
- 部署 = exe + 配置/档案文件，随目录整体拷贝即完成迁移

---

## 一、解决什么问题

显示器设置只能靠机身按键操作，总归不好用，有些人还不会用。

更麻烦的是，显示器恢复出厂设置后会默认进入省电模式，这时候通过 DDC/CI 无法进行控制。

所以可以提前对当前设置进行保存，把它作为「基准」：

- 先进行恢复出厂设置；
- 再退出省电模式；
- 保存当前设置为基准。

这样以后就能像「恢复基准」一样，随时恢复成默认出厂设置了。

也可以把自己常用的一些设置保存为基准，随时一键还原。

对于网吧，显示器参数经常被顾客调乱：亮度拉满、对比度归零、色彩模式乱切。
下一个顾客不会调，网管也懒得一台台去按显示器按键。

后面可能还需要增加多基准功能。

本程序把显示器参数变成软件可读写的数据，于是可以：

1. 调好一台 → **保存基准**
2. 之后任何人乱调 → **一键还原基准**（可全店静默执行）
3. 开机 / 下机 / 闲置超时 → **自动还原**

---

## 二、快速上手（网管 30 秒版）

1. 把 exe 放到客户机，双击运行 → 自动识别显示器型号
2. 把显示器调到想要的标准状态（本软件或显示器自带 OSD 都行）
3. 点右上角 **保存基准**（或按 `Ctrl+Alt+S`）
4. 之后点 **一键还原基准**（或按 `Ctrl+Alt+R`）→ 立刻恢复

建议每台客户机各存一份自己的基准（档案按「型号 + 序列号」区分）。

## 三、操作方式

标准应用窗口：顶部选择显示器，中间是标签页分栏（图像 / 显示 / 系统…，按品牌不同），
右侧「一键还原基准 / 保存基准」按钮；关闭窗口只是隐藏到托盘，托盘右键菜单里可真正退出。

| 操作 | 方式 |
|---|---|
| 调整参数 | 拖动滑条 / 选择下拉项，即改即生效 |
| 切换分类 | 点击标签页 |
| 多显示器切换 | 顶部下拉框，或热键 `Ctrl+Alt+←` / `Ctrl+Alt+→` |
| 打开 / 隐藏窗口 | 热键 `Ctrl+Alt+O`，或托盘图标双击 |
| 一键还原（全部显示器） | 右上角按钮，或 `Ctrl+Alt+R` |
| 保存基准（当前显示器） | 右上角按钮，或 `Ctrl+Alt+S` |
| 置灰的项目 | 该型号未开放 DDC/CI，已注明原因；请用显示器自带按键调整 |

## 四、自动还原（无人值守）

菜单 系统 → 软件设置 里可开：

- **启动时自动还原全部基准**：配合开机自启，每台机器开机就是标准状态
- **注销/关机时自动还原**：顾客下机重启后即还原
- **闲置 N 分钟自动还原**：顾客调完离开，超时自动恢复

开机自动还原推荐用计划任务（比启动项更可靠，管理员权限）：

```cmd
schtasks /create /tn "MonitorOSD还原" /sc onlogon /rl highest /tr "\"D:\MonitorOSD\MonitorOSD.exe\" /restore"
```

`/restore` 是静默还原模式：还原所有显示器的基准后自动退出，不显示界面。

## 五、命令行（可写进脚本）

> 下文用 `MonitorOSD.exe` 代指主程序文件；当前构建产物名是「金橙电竞-显示器控制.exe」，可自行改名。

```cmd
MonitorOSD.exe /restore            :: 还原所有显示器的基准后退出（可挂开机任务）
MonitorOSD.exe /set 0x10 80        :: 向所有显示器写入指定 VCP 参数（亮度=80）
MonitorOSD.exe /set 0x10 80 /verbose   :: 同上，弹窗显示结果
MonitorOSD.exe /detect             :: 列出显示器型号、DDC 能力、菜单支持情况 → detect.txt
MonitorOSD.exe /scan               :: 全量扫描 0~255 所有 VCP 码 → scan_report.txt
```

## 六、品牌支持情况

**卓威 XL2566K / XL2746K（实机差分校准验证）**
- 可调：亮度、对比度、锐利度、Gamma、色温（含 USER 的 RGB）、低蓝光、输入源、显示模式、
  Smart Focus、色彩格式、耳机音量、恢复出厂
- 差分实测解锁的隐藏码：
  **DyAc=0x4D**（关/高/高级）、**AMA=0xF0**（关/低/中/高）、
  **黑位均衡器=0x7A**（0~20）、**自然色彩饱和度=0x89**（0~20）、
  **显示模式=0x86**
- BenQ 通用补充：低蓝光 `0x19`、自动关机 `0xF8`、FreeSync `0x71`、色域 `0x5F`、均匀性 `0xFA`

**华硕 ROG / TUF（值序来自对官方 DisplayWidgetCenter 的逆向 + XG27AQDMGR 实机验证）**
- 可调：亮度、对比度、锐利度（VividPixel）、Trace Free、Shadow Boost、滤蓝光、ELMB、
  GameVisual 场景模式、色温、Gamma、饱和度、用户 RGB、输入源、音量/静音、
  GamePlus（准星/计时/FPS 计数）、恢复出厂
- 暂灰显：Adaptive-Sync（走显卡驱动）、ASCR（码未确认）

**AOC / 飞利浦（AGON 等；显示模式值序参考社区项目 Theluga/Monitor_Mode_AOC）**
- 可调：亮度、对比度、锐利度、显示模式（标准/FPS/RTS/赛车/电竞 1/2/游戏 3）、暗部控制、
  响应加速、伽马、色温、输入源、音量、OSD/按键锁定

**OSD/按键远程锁定（MCCS 2.2 标准码 `0xCA`，各品牌通用）**
- 系统页「OSD/按键锁定」：锁菜单（按键仍可用）/ 全锁（菜单 + 按键全失效），选「正常」恢复

**其他品牌**：通用菜单（亮度/对比度/输入/音量等标准码），扫到什么支持什么。

> 安全规则：`0x04 / 0x08 / 0x0C` 是「恢复出厂 / 保存设置」动作码，程序已列入黑名单，
> 扫描器、校准向导、普通菜单都不会触碰，防止误触发复位。

## 七、差分校准向导（新型号 10 分钟点亮功能）

不同型号固件不同，遇到「某项实际能调但程序置灰」时不用等更新：

1. 系统管理 → **差分校准向导** → 点 **扫描对比**（首次建立基准，约 30 秒）
2. 去显示器 OSD 修改**一项**设置（比如 DyAc 调一档）
3. 回来再点 **扫描对比** → 自动列出值变化的码
4. 选中正确的码 → 下拉框指认功能名 → **保存绑定**（写入 `vcpmap.json`，重启生效）

之后每轮「改一项 → 点一次」即可，整个过程只建一次基准，每轮 10 秒。

## 八、自定义映射（vcpmap.json，不用重新编译）

exe 目录放一个 `vcpmap.json` 覆盖内置映射（写法见仓库根目录 `vcpmap.json.example`）：

```json
{ "ZOWIE": { "beqexp": { "code": "0x7A" } } }
```

菜单 ID 见程序内置表（`bright / contrast / sharp / gamma / lbl / colortemp / input / dmode /
volume / beqexp / vibexp / dyacexp / amaexp / zsatexp` 等），改完重启程序生效。

## 九、常见问题

**显示器恢复出厂后软件全灰 / 调不动（卓威新机型节能假死）**
卓威近年机型恢复出厂后会自动进入节能/ECO 状态，面板 DDC 假死。程序对策：
系统页 **「唤醒显示器」** 按钮 + 还原时自动唤醒 / 回读校验 / 3 次重试。
根治：重置后立刻在 OSD 里手动关闭节能/ECO 再保存基准。

**提示「厂商未开放软件控制」**
该功能没接 DDC/CI（与显示器里的 DDC/CI 开关无关），程序置灰并注明原因。
可用差分向导试探是否存在隐藏码。

**笔记本内置屏不行**
内置面板不走 DDC，外接的可以。

**两台同型号显示器**
档案按「型号 + 序列号」区分；系统只报得出型号时两台共用一份基准。

**客户机杀毒软件报毒？**
程序用的是微软官方 `dxva2.dll` 标准 API，无加壳、无联网（除可选的在线更新/反馈）；
个别杀软误报加白名单即可。

**文件说明**（都在 exe 同目录，随目录整体拷贝/删除）
- `profiles.json` 基准档案　`config.json` 配置　`vcpmap.json` 自定义映射（可选）
- `log.txt` 操作日志　`scan_型号.txt` 扫描报告

## 十、从源码编译

```
cpp\build.bat
```

需要 **Visual Studio 2022 BuildTools** 的 C++ 工作负载（MSVC x64 + Windows SDK 10）。
`build.bat` 里写死了作者本机的 MSVC / SDK 路径，请按自己的安装位置修改；
也可以直接在 VS 的 x64 命令行环境里手工执行其中的 `cl` 命令。
产物为 `cpp\金橙电竞-显示器控制.exe`。

> `金橙电竞` 只是程序内置的可配置标题前缀（`config.json` 的 `TitlePrefix`，可为空），
> 程序本名是「显示器控制」。首次运行会自动生成 `config.json`，
> 仓库里的 `cpp/config.example.json` 是默认配置样例。

运行所需：exe 同目录需要 `Fonts\`（仓库已附带 4 个子集字体；缺失时回落系统字体）。

## 十一、目录结构

```
cpp/
  main.cpp              全部逻辑：DDC/CI 通讯、能力解析、品牌识别、码表、中文菜单树、
                        基准档案/配置/日志、主界面/托盘/热键、网管工具、在线更新/反馈、命令行
  build.bat             MSVC 单文件构建（版本号注入）
  app.rc / app.ico      图标与版本资源
  logos_embed.h         品牌 Logo 图像数据
  config.example.json   默认配置样例
  server/               在线更新与意见反馈服务端（PHP + 本地联调 Python）
  docs/                 服务端部署手册
docs/
  scan_XL2566K.txt      VCP 能力与全码扫描实测记录（码表依据）
Fonts/                  界面字体子集（Noto Sans SC / JetBrains Mono，OFL 1.1）
UI/                     设计规范、Design Tokens、设计说明书
.github/                Issue / PR 模板
```

## 十二、参与贡献

最需要的贡献不是代码，而是**实机显示器数据**——每多一个型号的码表，软件就多支持一批屏幕。
具体做法见 [CONTRIBUTING.md](CONTRIBUTING.md)，或直接提「新型号适配」Issue。

## 十三、许可证

本项目以 **GNU General Public License v3.0** 发布，全文见 [LICENSE](LICENSE)。

- 允许个人使用，也允许商业使用（例如网吧部署、收费维护）；
- 允许修改和再分发，但**修改后的版本必须同样以 GPL-3.0 开源**，并保留版权声明；
- 本程序不提供任何担保，使用风险自负。

版权所有：Copyright (C) 2026 咸鱼工作室 / 飞翔的咸鱼。

字体文件为 Noto Sans SC 与 JetBrains Mono，均以 SIL Open Font License 1.1 授权，
许可全文见 `Fonts/OFL-NotoSansSC.txt` 与 `Fonts/OFL-JetBrainsMono.txt`。

## 十四、致谢

- DDC/CI 与 MCCS 标准（VESA）
- AOC 显示模式值序参考：Theluga/Monitor_Mode_AOC
- 字体：Google Noto Sans SC、JetBrains Mono
