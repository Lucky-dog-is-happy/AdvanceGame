# Advance Game

一款终端下的个人任务管理 RPG 游戏。把日常任务变成"任务"，把技能当作属性，
把每日复盘写成 Markdown 日记——全在复古风格的命令行里完成。

## 功能

- **玩家系统** — 姓名、年级、专业、等级 & 经验条
- **技能管理** — 添加/删除技能；完成关联技能的任务可获得技能经验
- **任务系统** — 添加任务（截止日期、难度 Easy/Medium/Hard、重要性 Minor/Normal/Major/Fatal、经验奖励）
- **完成任务** — 标记任务完成获得经验，累积经验自动升级
- **复盘日记** — 从模板自动生成每日 Markdown 笔记，支持 `$EDITOR` 编辑、浏览、删除
- **终端界面** — ANSI 彩色表格、进度条、智能排序（未完成优先，按截止日期 & 优先级排列）
- **原始模式行编辑器** — `←`/`→` 移动光标、Backspace/Delete 删除、完整 UTF-8 与 CJK 支持

## 命令

| 按键 | 作用 |
|------|------|
| `h`  | 显示帮助 |
| `i`  | 显示玩家信息 / 技能 / 任务 |
| `as` | 添加技能 |
| `at` | 添加任务 |
| `ar` | 生成今日复盘 |
| `ds` | 删除技能 |
| `dt` | 删除任务 |
| `dr` | 删除复盘 |
| `t`  | 列出任务并标记完成 |
| `r`  | 浏览复盘 |
| `q`  | 保存并退出 |

## 数据文件

- `AdvanceGame.dat` — 二进制存档（玩家、技能、任务）
- `retrospectives/template.md` — 每日复盘用的 Markdown 模板
- `retrospectives/YYYY-MM-DD.md` — 每日复盘记录

## 编译与运行

```bash
gcc -o advancegame AdvanceGame.c
./advancegame
```
