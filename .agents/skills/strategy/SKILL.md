---
name: strategy
description: Build or refine a complete nine-section Product Strategy Canvas. Use when the user invokes $strategy, writes /strategy in a message, or requests the full product strategy workflow.
---

# Strategy

Adapted from [phuryn/pm-skills strategy](https://github.com/phuryn/pm-skills/blob/8607e3b077817f89bf4a9b623246219734ac3be0/pm-product-strategy/commands/strategy.md), under the [MIT license](../../pm-skills-LICENSE). This project-local Codex skill uses `$strategy`; it does not register a native slash command.

Use the user's language and reuse project context, existing strategy, research, and supplied documents. Ask only about material gaps: product, audience, stage, business model, and the decision prompting this strategy. If no subject can be inferred, ask for one.

Read and apply [product-strategy](../product-strategy/SKILL.md) and [product-vision](../product-vision/SKILL.md). Develop all nine sections:

1. Vision: an inspiring, achievable statement in 2–3 sentences.
2. Target segments: pain, current alternatives, priority, primary audience, and explicitly excluded audiences. Include segment size only with evidence or a labeled estimate.
3. Pain points and value: problems, current costs, and value delivered for each segment.
4. Value propositions: situation, motivation, and desired outcome using JTBD.
5. Strategic trade-offs: what to choose, what to forgo, and why.
6. Key metrics: North Star, 3–5 inputs, and health guardrails.
7. Growth engine: specific acquisition, activation, and expansion mechanisms where relevant.
8. Core capabilities: build, buy, or partner; investment and timeline.
9. Defensibility: credible advantages and gaps; acknowledge when no moat exists.

Include date, product stage, author if known, the top three strategic risks, and proposed next steps. Label hypotheses, unknowns, and provisional targets. For a personal or noncommercial product, preserve that scope; explain where commercial growth or defensibility is inapplicable instead of inventing a business model.

Save Markdown in the established documentation location, defaulting to `docs/strategy/<topic>.md`. Check for an existing document and preserve unrelated content. Link the result. Offer a one-page version or a relevant follow-up such as a canvas, roadmap, environmental scan, or OKRs; do not begin implementation or external actions from this planning request.
