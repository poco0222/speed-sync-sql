---
name: discover
description: Run a complete product discovery cycle from ideas through assumption mapping, prioritization, and experiment design. Use when the user invokes $discover, writes /discover in a message, or requests a full product discovery workflow.
---

# Discover

Run a guided discovery cycle for the product, feature, or opportunity supplied after `$discover` or in the conversation. This is a project-local Codex adaptation; it does not register a native `/discover` slash command.

Adapted from [phuryn/pm-skills discover](https://github.com/phuryn/pm-skills/blob/8607e3b077817f89bf4a9b623246219734ac3be0/pm-product-discovery/commands/discover.md), under the [MIT license](../../pm-skills-LICENSE).

Use the user's language. Reuse existing project context and research before asking questions. Preserve the project's product scope. Discovery produces a validation plan; it does not authorize implementation, launching experiments, contacting users, or publishing material.

## 1. Establish context

Determine whether the subject is an existing product with real users or a new product without validated demand. An existing repository alone does not establish validated demand.

Clarify only missing information: the opportunity being explored, available research or feedback, and the decision this work should inform. If no topic was provided, ask for it before brainstorming. Explain that the guided workflow usually takes 15–30 minutes and has two selection checkpoints; the user can redirect or skip stages.

## 2. Generate ideas

Read and apply the relevant skill:

- Existing product: [brainstorm-ideas-existing](../brainstorm-ideas-existing/SKILL.md).
- New product: [brainstorm-ideas-new](../brainstorm-ideas-new/SKILL.md).

Explore PM, Designer, and Engineer perspectives. Present the top 10 ideas with brief rationale, grounded in available evidence.

Checkpoint: ask which 3–5 ideas to stress-test, with the option to carry all forward. Wait for selection unless the user has already authorized you to choose or complete the process autonomously.

## 3. Surface assumptions

Read and apply the relevant skill to the selected ideas:

- Existing product: [identify-assumptions-existing](../identify-assumptions-existing/SKILL.md).
- New product: [identify-assumptions-new](../identify-assumptions-new/SKILL.md).

Compile a deduplicated list across the skill's risk categories, covering value, usability, feasibility, and viability, plus the additional new-product categories where applicable. Distinguish research evidence from inference and untested assumptions.

## 4. Prioritize assumptions

Read and apply [prioritize-assumptions](../prioritize-assumptions/SKILL.md). Rank assumptions by impact and uncertainty, identify leap-of-faith assumptions, and group those that can share an experiment.

Checkpoint: ask which critical assumptions to validate first. Wait unless the user has already delegated this choice. When choosing autonomously, state the chosen priorities and rationale.

## 5. Design experiments

Read and apply the relevant skill:

- Existing product: [brainstorm-experiments-existing](../brainstorm-experiments-existing/SKILL.md).
- New product: [brainstorm-experiments-new](../brainstorm-experiments-new/SKILL.md).

Design 1–2 experiments per critical assumption. Specify hypothesis, setup, measurement, success and failure criteria, effort, and timeline. Sequence by dependencies and effort. For new products, emphasize desirability before feasibility; for existing products, use available usage data. Label proposed thresholds and estimates as provisional where evidence is missing. Do not report planned experiments as completed validation.

## 6. Save the discovery plan

Save a Markdown document in the project's established documentation location, defaulting to `docs/discovery/<topic>.md`. Check for an existing plan before creating or updating it; preserve unrelated content.

Include:

- Topic, date, product stage, and discovery question.
- Available evidence and its sources.
- Ideas explored and selected ideas with rationale.
- Assumptions table: ID, assumption, category, impact, uncertainty, priority.
- Experiments table: ID, assumption IDs, method, success criteria, effort, timeline.
- Experiment details: hypothesis, setup, measurement, and decision criteria.
- A proposed timeline reflecting actual dependencies and known constraints.
- Decision rules for proceeding, pivoting, stopping, or investigating further.

Return a link to the plan and the main unresolved risk. Offer a relevant next step such as a PRD, interview script, metrics plan, or user stories; do not start it without a request.
