# BMS V1 Git 基线建立任务报告

- 日期：2026-08-14
- 任务：将已验证的 Phase 3 工程纳入 Git 本地版本管理（仅本地，不创建远程仓库、不 push）
- 结论：**GIT CHECKPOINT: RECORDED**；分支 `dsh/phase4` 的后续证据已纳入最终 Release Baseline
- 约束：本任务不修改任何 BMS 源码/配置/Keil 工程/测试/报告内容（仅一处必要的 uvprojx 恢复，见 §5）

---

## 1. 任务目标

用户（Git 初学者）要求在不改动 Phase 3 已验证基线内容的前提下，由助手负责检查、安装、初始化并提交 Git：

1. 检查/安装 Git（优先 winget，回退官方安装器）；
2. 在项目根 `Bms_shop/` 初始化仓库，主分支 `main`；
3. 建立项目专用 `.gitignore`，排除构建产物与本机敏感信息；
4. 审查首次 `git status`，确认无密钥/token/许可证/巨型产物；
5. 设置 repository-local Git identity；
6. 创建 Phase 3 baseline commit；
7. 创建 annotated tag `phase3-validated`；
8. 从 tag 创建分支 `dsh/phase4` 并切换；
9. 之后停止，等待 Phase 4 授权。

---

## 2. 环境检查与 Git 安装

### 2.1 初始状态

- `git --version`：**未安装**（命令不存在）。
- winget：`winget.exe` 存在于 `WindowsApps` 但为 **0 字节损坏 stub**，真实 winget 亦报错（`0x8A150001`），重注册 App Installer 被拒（需管理员），winget 路线不可用。

### 2.2 网络诊断（与用户 VPN 的关系）

| 检查项 | 结果 |
|---|---|
| 系统代理 | `127.0.0.1:7890`（用户 VPN/Clash 类客户端），HTTP 出网正常（`gstatic.com/generate_204 → 204`） |
| github.com 直连 | 21 s 超时（很可能被墙/分流直连不通） |
| HTTPS/TLS | 沙箱拒绝访问系统 TLS 凭据（`SEC_E_NO_CREDENTIALS` / 证书吊销 `0x80092013`），**任何 HTTPS 都会失败** |

**结论**：网络问题与 VPN 部分相关（直连 github 不通、代理 TLS 证书链与沙箱冲突），但根因是**会话文件沙箱限制 TLS 凭据访问**。经用户授权提升权限 + 走本地代理下载。

### 2.3 安装结果

- 安装包：官方 `Git-2.55.0.4-64-bit.exe`（65,388,168 字节，SHA-256 `0CBC0B34A74B3AFF3ACE0910328549155A770E228331B19CB1498218A120E7FF`）
- 来源：GitHub Releases（git-for-windows v2.55.0.windows.4）
- 方式：Inno Setup 静默安装（`/VERYSILENT /NORESTART /SP-`），退出码 0
- 安装位置：`C:\Program Files\Git`（机器 PATH 已含 `C:\Program Files\Git\cmd`）
- 验证：`git version 2.55.0.windows.4`

> 未安装 GitHub Desktop；未创建 GitHub repository；未 push；仅本地仓库。

---

## 3. 仓库初始化

| 项目 | 值 |
|---|---|
| 仓库根 | `D:\AI\Codex\Bms_shop`（含 docs/、deliverables/、firmware/） |
| 初始化前备份 | 用户已确认保留 Phase 3 文件夹备份 |
| `git init` | 新建（此前无 `.git`） |
| 默认分支 | 初始为 `master`，按规则重命名为 **`main`** |

---

## 4. .gitignore 与 .gitattributes（字节级证据保护）

### 4.1 决策依据

Phase 3 验证证据（报告/日志/manifest art-023）绑定的是**文件字节级 SHA-256**，且若干被引用文件（`FreeRTOSConfig.h`、SPL 源码、build log、map）在工作树中为 **CRLF/LF 混合行尾**。Git 默认 `core.autocrlf=true` 会做行尾转换，**会改变字节、破坏哈希证据**。

### 4.2 .gitignore（保留/忽略决定已记录于 `deliverables/review/BMS_V1_Git_基线提交说明.md`）

**保留**：全部源码（App/Config/Driver/User）、`docs/spec`、`docs/FreeRTOS`、`docs/reference`（TI/ST 输入资料，按用户要求不删不改）、`deliverables/*`（Phase 1–3 报告与 review）、Tests 源码/配置（`.c/.h/.py/.ini/.sct`）、`BMS_V1.uvprojx`、`BMS_V1.uvoptx`（Simulator 自动化入口）、`EventRecorderStub.scvd`、纯文本构建/执行证据（`Build/*.log`、`BMS_V1.map`、Tests/Build 下的 simulator/verify log）。

**忽略**：`Objects/`、`*.o/*.obj/*.crf/*.axf/*.bin/*.d/*.dep/*.lnp/*.lst/*.htm`、**含许可证的 `*.build_log.htm`**、`*.uvguix.*`（用户名嵌入文件名）、`tmp/`、`__pycache__/`、`*.pyc`、编辑器/系统临时文件、`.project-memory/*.lock`。

### 4.3 .gitattributes（新增防护）

全局 `* -text`（**所有文件按原样字节存储，禁止任何行尾转换**），二进制扩展名显式 `binary`。提交前后两次全量校验：194 个暂存文件与工作区字节一致（`MISMATCH=0`）。

---

## 5. 发现并修复：uvprojx 被 Keil IDE 改写

### 5.1 问题

`verify_phase3.py` 在提交前复跑时失败（`ARMCC5 lock drifted`）：

- Phase 3 验证时刻（13:31）`BMS_V1.uvprojx` 的 `pCCUsed` 为 `...::ARMCC`，报告引用哈希 `7155ec5f...`；
- 21:20 有人打开 Keil IDE（触发重建），IDE 自动把 `pCCUsed` 改写为 `...::.\Version5.06`，文件哈希变为 `179e8862...`；
- 差异导致：① 文件哈希与 Phase 3 报告引用不一致；② `verify_phase3.py` FAIL。

### 5.2 修复（经用户授权）

- 内存模拟证明：仅将 `pCCUsed` 还原为 `::ARMCC`，哈希**精确还原**为 `7155ec5f...`（证明 Keil 只改了这一处）；
- 执行单处还原，并将文件 mtime 恢复到验证时刻（13:31:30，早于产物），恢复时间线一致性；
- 复跑 `verify_phase3.py`：**6 项全部 PASS**（exit 0），`Code=3164 RO=268 RW=32 ZI=1896`。

### 5.3 影响面确认

- `Objects/`、`Listings/` 产物 mtime 为 21:20 重建，但 `BMS_V1.map` 哈希与报告引用精确一致（`828a29ad...`）；axf 为二进制不受字节证据约束且已被忽略；
- `uvoptx` 被 touch 但关键 Simulator 配置（`sIfile`/`RunSim`/`uSim`）未变，无报告哈希引用，无影响；
- 提交前 12 项关键证据哈希全部复核匹配。

---

## 6. 提交、tag 与分支

| 步骤 | 结果 |
|---|---|
| Git identity（repo-local） | `Tmdytnl <3344248696@qq.com>`（用户提供；全局配置未动） |
| baseline commit | `83bd3be9e5c2ee20e8c9ef7b1ac4bf88847c8d1a` — `baseline: validated through phase 3`（194 files, 96,060 insertions） |
| tag | `phase3-validated`（annotated，`BMS V1 validated through Phase 3`），已确认指向 `83bd3be` |
| 分支 | `dsh/phase4` 从 `phase3-validated` 创建并切换，`git branch --show-current` = `dsh/phase4` |

最终结构：

```text
main  ── (83bd3be) baseline: validated through phase 3
              ▲
              tag: phase3-validated
              └── dsh/phase4   ← 当前分支（此后 Phase 4 修改只在此分支）
```

---

## 7. 敏感信息与产物审查结果

| 检查项 | 结果 |
|---|---|
| 密钥/token/credentials | 未发现（全树正则扫描 `NO_SENSITIVE_MATCHES`） |
| 本机 Keil 许可证 | `Objects/BMS_V1.build_log.htm` 含 `LIC=8VRJ7-...` → **已忽略，未提交** |
| 用户目录/用户名 | `BMS_V1.uvguix.Tmdytnl`（文件名含用户名）→ 已忽略 |
| 巨型自动产物 | `tmp/Git-2.55.0.4-64-bit.exe`（62 MB）→ `tmp/` 整体忽略 |
| 正式大文件 | `docs/reference/**/*.pdf`（最大约 12.9 MB）→ 按用户要求保留为项目输入 |

---

## 8. 最终状态

```text
git --version        : git version 2.55.0.windows.4
repository root      : D:\AI\Codex\Bms_shop
.gitignore           : D:\AI\Codex\Bms_shop\.gitignore
.gitattributes       : D:\AI\Codex\Bms_shop\.gitattributes
baseline commit      : 83bd3be (baseline: validated through phase 3)
tag phase3-validated : 已创建，指向 83bd3be
current branch       : dsh/phase4
git status           : clean
```

**GIT BASELINE: READY**
**CURRENT BRANCH: dsh/phase4**

---

## 9. 待办 / 风险

- 未修改任何 BMS 源码、配置、测试或报告；`verify_phase3.py` 全 PASS，Phase 3 证据链完整。
- `BMS_V1.uvoptx` 内含一条本机绝对路径（Simulator 入口 `sIfile`），换机器后可能需要更新；已在提交说明中记录。
- 不要直接打开 Keil IDE 后保存工程，否则可能再次改写 `pCCUsed`（会重新造成哈希漂移）；如发生，重复 §5.2 的单处还原即可。
- 后续 Phase 4 及最终项目证据已沿受控分支链完成并纳入 Release Baseline。
