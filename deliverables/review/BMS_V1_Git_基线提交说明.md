# BMS V1 Git 基线提交说明（.gitignore 决定记录）

- 日期：2026-08-14
- 目的：记录建立 Git Phase 3 baseline 时对每个文件类别的保留/忽略决定，供后续审计。
- 原则：保留源码、规格、测试、报告与可复现工程配置；排除可重建的机器相关构建产物；本机隐私（许可证、用户名、绝对路径）不入库。

## 1. 保留（纳入版本控制）

| 类别 | 路径 / 模式 | 理由 |
|---|---|---|
| 源码 | `firmware/App/*.c/*.h`、`firmware/Config/*.h`、`firmware/Driver/*.c/*.h`、`firmware/User/main.c` | 项目源码 |
| 规格与参考输入 | `docs/spec/`、`docs/FreeRTOS/`、`docs/reference/`（ST/TI 官方资料、SPL、CMSIS） | 只读项目输入，按用户要求保留现有目录结构，不删除/移动/重命名 |
| 阶段报告 | `deliverables/phase1..3/`、`deliverables/review/` | 验证证据 |
| 测试源码与配置 | `firmware/Tests/*.c/*.h/*.py/*.ini/*.sct`、`firmware/Tests/README.md` | 可复现测试与独立 oracle |
| Keil 工程 | `firmware/Project/Keil/BMS_V1.uvprojx` | 工程配置（全部相对路径，无许可证/绝对路径） |
| Keil Simulator 配置 | `firmware/Project/Keil/BMS_V1.uvoptx` | 含 Simulator 自动化入口（`sIfile` 指向 `Tests/phase3_simulator.ini`），属于可复现测试配置；已知含一条本机绝对路径 `D:\AI\Codex\Bms_shop\...`，属可接受工程配置，保留并在此记录 |
| Keil 组件配置 | `firmware/Project/Keil/EventRecorderStub.scvd` | 无本机敏感信息 |
| 纯文本构建/执行证据 | `firmware/Project/Keil/Build/*.log`（3 份 phase build log）、`firmware/Project/Keil/Listings/BMS_V1.map`、`firmware/Tests/Build/Phase2|Phase3/*.log`（simulator/verify log） | 阶段报告明确引用为验证证据（如 art-013/art-019/art-020/art-021），按用户规则"关键纯文本 build log 作为验证证据，原则上保留" |
| 内存预算 | `firmware/Project/SRAM_Budget.md` | 项目文档 |
| Git 说明 | `.gitignore`、本文件 | 版本管理元数据 |

## 2. 忽略（不纳入版本控制）

| 类别 | 模式 | 理由 |
|---|---|---|
| Keil 自动生成对象/产物 | `firmware/Project/Keil/Objects/`、`*.o/*.obj/*.crf/*.axf/*.bin/*.d/*.dep/*.lnp/*.lst/*.htm`、`Objects/BMS_V1.sct` | 可由工程重建，机器相关 |
| **含本机许可证的 HTML 构建报告** | `*.build_log.htm`（尤其 `Objects/BMS_V1.build_log.htm`） | 已确认含 `License Information: T Tmdytnl ... LIC=8VRJ7-...`、本机工具链路径 `D:\Keil_v5\...`，属本机隐私，**禁止提交** |
| Keil 用户 GUI 状态 | `*.uvguix.*`（如 `BMS_V1.uvguix.Tmdytnl`） | 文件名嵌入 Windows 用户名，本机临时 UI 状态 |
| 测试构建中间产物 | `firmware/Tests/Build/**/*.o`、`*.axf` | 可重建；同目录 `*.log` 证据保留 |
| 临时工作区 | `tmp/`（含 `tmp/pdfs/` 截图、下载的安装包） | 临时文件 |
| Python 缓存 | `__pycache__/`、`*.pyc` | 自动生成 |
| 编辑器/系统临时文件 | `*.tmp/*.bak/*.orig/*.swp/*~`、`Thumbs.db`、`Desktop.ini`、`.DS_Store` | 本机临时状态 |
| 项目记忆运行时锁 | `.project-memory/*.lock` | 运行期锁文件，非内容 |

## 3. 保留但需注意

- `BMS_V1.uvoptx`：含一条本机绝对路径（Simulator 入口），换机器后可能需要更新；不影响作为当前 Phase 3 测试配置证据。
- `docs/reference/**/*.pdf`：体积较大（最大约 12.9 MB），属项目只读输入，按用户要求保留。
- `BMS_V1.map`：由链接器重建，但阶段报告引用其哈希作为证据，故保留当前版本。

## 4. 未发现

- 密钥 / token / credentials：未发现。
- 除上述 `build_log.htm` 外的其他本机许可证信息：未发现。
