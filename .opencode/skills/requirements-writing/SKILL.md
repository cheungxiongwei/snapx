---
name: requirements-writing
description: Use when the user asks to write, create, update, rewrite, or refine a requirements document (requirements.md, 需求文档, 需求规格, PRD) from their input, feature description, or an existing product's behavior. Extracts needs from what the user says and grounds them against the actual product, then writes or updates requirements.md in a product-manager voice.
---

# Writing requirements.md

Produce a `requirements.md` that reads like a product manager wrote it, from
whatever the user gives you (a feature idea, a behavior description, a bug
report, a rough list). If the file does not exist, create it. If it exists,
rewrite it in place to match reality rather than appending.

This skill is project-agnostic. Discover the product's language, terminology,
and structure from the repository in front of you; never import assumptions
from another project.

## Core stance

- **Product voice, not engineering voice.** Describe what the user needs and
  what the product does for them. Ban API names, libraries, class names, source
  file paths, test-framework names, internal data structures, and specific
  exit-code numbers. Prefer "用户可把结果写入标准输出以接入管道" over a
  function-class-and-flag description.
- **Requirements are needs, not a feature list.** Each bullet under 需求 answers
  "what does the user need to accomplish and why", so a reader who has never
  seen the code understands the motivation.
- **Ground everything against reality.** Before writing, read the existing docs
  and the source to confirm what the product actually does. If the user's input
  contradicts the implementation, the implementation wins for 行为/约束 unless
  the user is explicitly asking for a change. Delete stale claims rather than
  preserving them.
- **Remove what no longer holds.** A requirements doc that promises unsupported
  behavior or wrong constraints is worse than a short one. When a claim no
  longer matches the product, delete it. Do not keep "for history".
- **Describe behavior, not mechanism.** "同一标识对应唯一确定的结果" is
  behavior. Internal priority rules and fallback logic are mechanism and belong
  in a design/spec document, not here.
- **Match the product's own voice.** Use the terminology the product already
  uses (in its README, CLI help, or UI strings) for names and concepts.

## Document structure (follow exactly)

```markdown
# <产品名> 需求

<一段产品定位引言：它是什么、给谁用、解决什么问题。链接相关的设计或接口文档（若仓库中存在）。>

需求：

- <从用户视角陈述的需求，5-8 条，覆盖核心动机与关键边界>
- ...

### 能力

<一句话总起。然后展示产品对外提供的能力，通常是一组加粗词条 + 说明；可含一句输出格式/选项的高层描述。>

### 行为

- <可观察的产品行为，写成陈述句。覆盖：确定性、边界输入、命名、输出去向、成功回执、失败信号。>
- ...

### 约束

- <不可协商的限制：非交互性、支持范围、平台、已知固有限制、覆盖策略、错误语义。>
- ...

### 验证

- <验收动作，每条都是可执行的：做什么、确认什么。避免断言实现细节。>
- ...
```

Section headings are `###` under a single `#` title; the top-level needs list
under 需求 is an un-numbered bullet list, not a heading. Keep the literal
Chinese headings 需求 / 能力 / 行为 / 约束 / 验证 unless the user asks for
another language — the structure is the contract, not the labels.

## How to extract needs from user input

1. **Read the input for outcomes, not features.** "I want to capture a window by
   process id" → need: *以进程标识精确指定目标，避免同名目标歧义*, plus the
   outcome it enables.
2. **Ground against the real product.** Inspect the repo for the sources of
   truth: README, existing docs, CLI help/usage text, public interfaces, and
   source. Confirm supported operations, inputs, outputs, and failure modes.
   Correct the user where the product already does more or less than they
   assumed, and say so in your summary.
3. **Ask only if a need is truly ambiguous.** Prefer inferring the obvious need
   and grounding it against the product over interrogating the user. If you
   guess, keep the guess at the level of "need" so it stays safe.
4. **Group into the five sections.** A raw need like "works without a GUI"
   becomes: a 需求 bullet (无人值守下完成), a 行为 (no confirmation dialogs), a
   约束 (完全非交互), and a 验证 (headless run succeeds).
5. **Cover the seam cases as behavior or constraints**: missing/unknown target,
   minimal/zero-size or empty output, existing output, unavailable capability,
   platform limits.

## Rules for each section

- **需求**: 5–8 bullets. Each is a user-outcome statement, often "用户可…" /
  "用户能得到…". The final bullet may address consumability or downstream use
  (e.g. results consumable by automation) when relevant.
- **能力**: answer "what can a user do at all". Enumerate the distinct
  operations. Mention output formats and high-level knobs only, never the
  option flags.
- **行为**: answer "what happens when". Include determinism, boundary inputs
  (missing target, existing output), auto-naming or defaults, output
  destinations, success receipt, and how failures differ from usage errors.
- **约束**: answer "what is fixed and cannot be negotiated". Non-interactivity,
  supported formats/targets, platform support, known system-imposed artifacts,
  overwrite policy, and the stable error semantics (described in words, not
  numbers).
- **验证**: answer "how do we know it works". Every item is an action plus an
  assertion. Mirror the 行为/约束 list so nothing claimed goes unverified.

## Workflow

1. Determine the target path: `requirements.md` by default; honor the path the
   user names. Projects commonly keep it at `docs/requirements.md`.
2. If it exists, read it and read the grounding sources (README, specs,
   architecture/design docs, CLI help, public interfaces, source). Note every
   claim that is stale, unsupported, or implementation-level.
3. Extract and group needs from the user's input plus the grounding sources.
4. Rewrite the file wholesale in the structure above. Create parent directories
   if the file is new.
5. Preserve links to sibling docs when the repo has them, using relative links.
6. Report what was created/updated and which stale claims you removed or
   corrected, with evidence (e.g. the source or doc that contradicts a claim).

## Quality checklist

- [ ] File is at the target path; created if missing.
- [ ] Follows the exact section structure: 需求 / 能力 / 行为 / 约束 / 验证.
- [ ] Reads in a product voice; no API names, file paths, class names, or raw exit codes.
- [ ] Every 行为/约束 claim matches the real product; contradictions resolved in the product's favor.
- [ ] Stale or unsupported claims deleted, not preserved.
- [ ] 需求 has 5–8 motivation-level bullets.
- [ ] 验证 items are executable and assert observable outcomes.
- [ ] Links to related docs preserved or added when those docs exist.
- [ ] No assumptions imported from other projects; terminology matches this product.
