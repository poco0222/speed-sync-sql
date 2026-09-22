---
name: write-prd
description: Turn a feature idea, problem statement, or brief into a scoped eight-section PRD with requirements and acceptance criteria. Use when the user invokes $write-prd, writes /write-prd in a message, or requests the guided PRD workflow.
---

# Write PRD

Adapted from [phuryn/pm-skills write-prd](https://github.com/phuryn/pm-skills/blob/8607e3b077817f89bf4a9b623246219734ac3be0/pm-execution/commands/write-prd.md), under the [MIT license](../../pm-skills-LICENSE). This project-local Codex skill uses `$write-prd`; it does not register a native slash command.

Use the user's language. Accept a feature, problem, user request, rough idea, or document. Reuse project scope and supplied research. If no subject can be inferred, ask for one. Clarify only material gaps, prioritizing the user problem, affected audience and workaround, success metrics, constraints, prior attempts, and scope or phasing preference.

Read and apply [create-prd](../create-prd/SKILL.md). Produce a draft with date, author and stakeholders if known, and these eight sections:

1. Executive summary: what, for whom, and why now in 2–3 sentences.
2. Background and context: problem, attributed research, prior attempts, and trigger.
3. Objectives and success metrics: goals, explicit non-goals, and a table of metric, current value, target, and measurement.
4. Target users and segments: affected users, profiles, and sizing where supported.
5. User stories and requirements: P0 must-haves, P1 should-haves, and P2 future candidates where justified, each with observable acceptance criteria.
6. Solution overview: high-level approach and established design or technical constraints.
7. Open questions: unresolved question, owner, and deadline where known.
8. Timeline and phasing: milestones and dependencies, with estimated dates labeled.

Keep scope tight and preserve the project's agreed boundaries. If an idea is oversized, propose phases; distinguish a recommended Phase 1 from user-approved scope. Do not invent research, baselines, owners, deadlines, or agreed targets. Use explicit unknowns or provisional proposals. A draft PRD does not authorize implementation.

Save Markdown in the established documentation location, defaulting to `docs/prd/<topic>.md`. Check existing documents before writing and preserve unrelated content. Return a link and important unresolved questions. Offer scope tightening, a pre-mortem, detailed user stories, or a stakeholder update as relevant; do not send messages or begin follow-up work without a request.
