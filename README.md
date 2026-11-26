# 海战算法 v4 · 公开挑战

写一个 C++20 策略，在四张公开海图上挑战强、弱两档陪练。你的程序每拍读取战场观察，控制舰队和航空中队，通过占领据点和击沉敌舰得分。可以用同一份程序换边，比较两边的胜负和比分。

## 安装依赖

- [Node.js 24 LTS](https://nodejs.org/en/download)，用于运行练习脚本和裁判。构建时需要同版本的 Node.js C/C++ 头文件。若安装包把头文件放在其他位置，可在 `make` 时传入 `NODE_INCLUDE=/路径/include/node`，指向含 `node_api.h` 的目录。
- [WASI SDK 34](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-34)，下载与系统、CPU 架构匹配的压缩包并解压。构建时用 `WASI_SDK` 指向解压后包含 `bin/clang++` 的目录。
- GNU Make 和支持 C++20 的编译器。macOS 可以通过 `xcode-select --install` 安装构建工具。Linux 可使用系统包管理器安装，Windows 用户可通过 WSL 使用 Linux 工具链。

## 跑第一局

在 GitHub 用 **Code → Download ZIP** 下载并解压，或克隆仓库。进入项目根目录后执行：

```sh
make WASI_SDK="/你的实际路径/wasi-sdk"
node match.mjs --map 1 --opponent weak
node match.mjs --map 4 --opponent strong --side blue
```

`make` 会生成本机裁判模块 `build/naval.node` 和选手程序 `player.wasm`。编辑 [main.cpp](main.cpp) 后，重新执行同一条构建命令。接口见 [规则第 11 节](规则.md) 和 [sdk/naval.hpp](sdk/naval.hpp)。本项目使用 Node.js 内置模块，直接运行即可。

## 练习参数

| 参数 | 含义 |
|---|---|
| `--map 1..4` | 选择公开图，默认 1。按编号依次为三路分流、东湾双点、环礁争心、斜岸错位。 |
| `--opponent strong` | 强档使用航迹预测、舰队协同和比分调度，默认执红，选手执蓝。 |
| `--opponent weak` | 弱档默认执蓝，选手执红。 |
| `--side blue/red` | 指定选手阵营，可使用同一份程序与同一对手换边。 |
| `--seed N` | 默认 11，可取 0 至 2147483647。正式赛固定为 11。 |

每局独占一个 `runs/` 子目录，可并行跑局。完成时退出码为 0，标准输出是一行 JSON，含选手视角的 `outcome`（win、loss、draw）、`score`（蓝、红）、`endTick`、实际加载的程序摘要以及两个结果文件的绝对路径。运行失败时返回非零，诊断写入标准错误。

## 解析赛果和回放

`result.json` 保存完整赛果。`replay.jsonl` 可按行读取。首行为 `meta`，包含本局完整静态海图、种子、阵营及双方程序的 SHA-256。随后从 0T 到终局，每行一拍。

| 逐拍字段 | 含义 |
|---|---|
| `observation` | 选手实际收到的己舰、据点、烟区、敌情、来袭物和上拍动作反馈。技能就绪拍、强化弹药位图、锁速和护盾存于各己方单位记录中。 |
| `actions`、`actionFeedback` | 本方本拍提交的动作及其受理结果，按零起的动作下标关联，包括终局拍。 |
| `truth` | 供赛后分析的拍初全图，含双方单位和在途投射物。实时决策可用字段以 SDK 的 View 为准。 |
| `events`、`score` | 实际发生在该拍的裁判事件与拍后蓝红比分。 |

事件的 `kind` 是名称。例如 `ShipLost` 表示沉舰，`SkillUsed` 的 `detail` 是技能编号、`targetId` 是目标己方单位编号。`ShoreImpact`、`ShellBlocked`、`AirCrash` 给出零起的岛屿下标 `island` 和位置 `x,y`。`ShieldAbsorbed` 的 `value` 是吸收伤害，`ReloadAdvanced` 的 `value` 是装填提前的拍数，`TorpedoCleared` 的 `objectId` 是被清除的鱼雷。技能与武器枚举见 SDK。

| kind | detail | value |
|---|---|---|
| ShotReleased | 投射物类型 | 单发伤害 |
| Impact | 投射物类型 | 本次命中伤害 |
| PlaneDamage | 中队成员下标 | 成员损失 HP |
| ShipLost | 保留 | 本拍累计伤害 |
| Ownership | 新归属方 | 保留 |
| ScoreChange | 新积分 | 本拍积分净变化 |
| FlakPoint | 开火单位类型 | 分摊到单机的防空伤害 |
| Spotted、LostContact | 接触单位类型 | 保留 |
| ScoreAward | 0 周期基础分、1 据点分、2 或 3 击沉相关分、4 零分下限修正 | 该项积分增减 |

```sh
node match.mjs --opponent weak --map 2 > last.json
python3 - <<'PY'
import json
from pathlib import Path

game = json.loads(Path("last.json").read_text())
print(game["outcome"], game["score"], game["endTick"])
with open(game["replayFile"]) as lines:
    for line in lines:
        row = json.loads(line)
        for event in row.get("events", []):
            if event["kind"] == "ShipLost":
                print(row["tick"], event["unitId"])
PY
```
