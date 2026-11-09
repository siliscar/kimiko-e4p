# Git 提交规范 / Git commit convention

本仓库今后的提交统一使用中文分类、中文标题和中文正文。文件名、函数名、版本号、协议名称等保留原文。历史提交不改写。本规范取代原有的 `type: summary` 和“正文可中英双语”约定。
All future commits use Chinese categories, subjects and bodies. Keep technical identifiers unchanged and preserve existing history. This replaces the former `type: summary` and optional bilingual-body rules.

## 1. 提交格式 / Format

```text
分类（范围）：一句话说明主要改动

1. 分类：具体改动。
2. 分类：具体改动。

验证：实际做过的检查及结果；未验证的内容如实注明。
```

- 标题只选一个主要分类；范围可省略，如 `文档：统一中文提交格式`。
- 标题建议不超过 30 个汉字，直接写“增加什么、修复什么、改善什么”，末尾不加句号。
- 标题后空一行，正文从 `1.` 开始连续编号，每项只说一件事；只有一项也保留编号。
- 明细按下表的分类顺序排列，同类放在一起；没有涉及的分类跳过，不写空项。
- 验证说明单独放在明细后。区分文档检查、主机测试、编译和真机测试，不把编译通过写成真机验证。
- agent 代为提交时，末尾另起一行写 `代理：Codex` 等真实名称。必要的 Git 元数据尾注（如 `Signed-off-by`）保留原格式。

Choose one main category for the subject; the scope is optional. Keep the subject short and concrete. After a blank line, number each change consecutively, even for a single item, and group entries in the category order below. End with truthful validation results and, for agent commits, a Chinese agent label. Preserve required Git metadata trailers.

## 2. 分类顺序 / Category order

| 顺序 / Order | 分类 / Category | 什么时候用 / Use for | 标题示例 / Subject example |
| --- | --- | --- | --- |
| 1 | 新增 / Feature | 增加原来没有的功能或操作入口 / New capabilities | 新增（阅读）：支持点击查看 EPUB 脚注 |
| 2 | 修复 / Fix | 解决错误、异常或不符合预期的行为 / Incorrect behavior | 修复（电源）：避免翻页时误锁屏 |
| 3 | 优化 / Performance | 改善速度、内存、功耗或显示效果 / Speed, memory, power or rendering | 优化（书架）：缩短封面加载时间 |
| 4 | 重构 / Refactor | 调整代码结构，保持对外行为不变 / Structure without behavior changes | 重构（显示）：合并重复的刷新逻辑 |
| 5 | 文档 / Documentation | 修改说明、规范或代码注释 / Documentation and comments | 文档：补充中文提交规范 |
| 6 | 测试 / Tests | 增加或调整测试用例、测试工具 / Test cases and tooling | 测试（阅读）：补充脚注点击用例 |
| 7 | 构建 / Build | 修改编译配置、依赖或持续集成流程 / Build, dependencies and CI | 构建：固定 ESP-IDF 工具链版本 |
| 8 | 维护 / Maintenance | 清理无用文件、调整仓库配置等日常维护 / Repository upkeep | 维护：清理未使用的演示资源 |
| 9 | 发布 / Release | 正式版本号、发布清单及交付文件 / Official release preparation | 发布：更新正式版至 v1.0.1 |
| 10 | 回退 / Revert | 撤销已有提交，正文写明提交编号和原因 / Undo a commit with its hash and reason | 回退（阅读）：撤销有兼容问题的排版调整 |

分类看改动目的：解决显示错误用“修复”，改善正常显示效果用“优化”；删除有效代码并调整结构用“重构”，清理无用资源用“维护”。功能改动附带的文档和测试写在同一提交明细中，标题仍用主要分类。仅做本地测试时继续沿用现有固件版本，不使用“发布”分类增加版本号。
Classify by intent: fixing a rendering defect is a fix, while improving normal rendering is an optimization. Restructuring active code is a refactor; removing unused assets is maintenance. Related docs and tests stay with their feature or fix. Local tests keep the current firmware version and do not trigger a release version bump.

## 3. 写法要求 / Writing rules

1. **先说动作，再说对象和结果。** 写“修复拔卡后书架不刷新”，不用“解决相关问题”。
2. **一句只说一件事。** 不把多个功能和多个修复塞进一长句。
3. **用具体词。** 不写“更新一下”“若干优化”“完善逻辑”“修复 bug”等看不出内容的话。
4. **只写本次实际改动。** 不复制聊天记录、内部计划或调试日志，不声称未做过的验证。
5. **一个提交解决一个问题。** 无关功能和修复拆开提交；相关实现、文档和测试可以一起提交。
6. **合并、压缩合并和回退也用中文。** 将工具自动生成的英文标题改成符合本规范的中文信息，再完成提交。

Use concrete action-first sentences, one change per item, and describe only actual work. Keep unrelated changes in separate commits. Apply the same Chinese format to merge, squash and revert messages, including tool-generated defaults.

## 4. 完整示例 / Complete examples

以下仅展示格式，不代表这些改动或验证已经执行。
These examples illustrate the format and do not claim completed work or checks.

```text
修复（电源）：避免阅读翻页时误锁屏

1. 修复：将按键和蓝牙翻页计入活跃时间。
2. 修复：过滤电源键的瞬时误触发。
3. 测试：补充连续翻页和电源键去抖用例。

验证：主循环主机测试通过；未做真机测试。
代理：Codex
```

```text
新增（阅读）：支持点击查看 EPUB 脚注

1. 新增：点击脚注标记展开注释。
2. 文档：补充脚注的打开和关闭方法。
3. 测试：补充脚注识别和点击区域用例。

验证：脚注主机测试通过；未编译固件，未做真机测试。
```

```text
文档：统一中文提交格式

1. 文档：明确提交分类及排列顺序。
2. 文档：补充编号明细和简洁写法示例。

验证：已检查文档格式和引用；仅修改文档，无需编译固件。
```
