---
name: north-star
description: Define and compare North Star Metric candidates, select one, and build supporting input metrics and guardrails. Use when the user invokes $north-star, writes /north-star in a message, or requests the full North Star definition workflow.
---

# North Star

Adapted from [phuryn/pm-skills north-star](https://github.com/phuryn/pm-skills/blob/8607e3b077817f89bf4a9b623246219734ac3be0/pm-marketing-growth/commands/north-star.md), under the [MIT license](../../pm-skills-LICENSE). This project-local Codex skill uses `$north-star`; it does not register a native slash command.

Use the user's language. Reuse project context. Clarify only missing essentials: product, user value, business model where applicable, current metrics, and why a new or revised North Star is needed. If no subject can be inferred, ask for one.

Read and apply [north-star-metric](../north-star-metric/SKILL.md).

## Workflow

1. Identify the relevant game: attention, transaction, or productivity. Explain the fit; do not invent monetization for a personal or noncommercial product.
2. Propose 2–3 candidate metrics. Assess each against seven criteria: expresses user value, leads revenue where applicable, measurable, understandable, actionable, not vanity, and resistant to gaming without real value. For noncommercial products, explain how sustained user value replaces the revenue criterion.
3. Recommend the strongest candidate with rationale. Define its formula, unit, time window, qualifying event, population, and exclusions sufficiently precisely to implement measurement. Distinguish a plausible leading indicator from a demonstrated relationship.
4. Identify 3–5 input metrics, their influence on the North Star, and owners where known. Aim for distinct, collectively explanatory levers; label unverified causal relationships. Do not imply an exact mathematical decomposition without one.
5. Add counter-metrics to protect quality and user outcomes from optimizing only the headline metric.

## Deliverable

Produce a framework containing:

- Business game and rationale.
- Candidate comparison and recommended North Star.
- Precise definition, rationale, current value if known, and provisional target.
- Seven-criterion validation table with evidence, uncertainty, or applicability notes.
- Inputs table: metric, influence, owner, current value, and target.
- A compact metrics tree connecting the North Star, inputs, and actions.
- Counter-metrics and the failure modes they guard against.
- Reasons for rejecting alternatives, based on this product rather than universal claims that activity or revenue metrics are always invalid.

Mark unknown baselines and proposed targets explicitly. Save Markdown in the established documentation location, defaulting to `docs/metrics/<topic>.md`; check existing documents and preserve unrelated content. Return a link. Offer a dashboard, OKRs, or measurement queries as relevant next steps. Do not add tracking, change analytics, or execute queries as part of this planning request.
