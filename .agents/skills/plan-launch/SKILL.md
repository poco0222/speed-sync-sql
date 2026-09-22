---
name: plan-launch
description: Create a go-to-market launch plan covering beachhead segment, ideal customer, positioning, channels, timeline, and success metrics. Use when the user invokes $plan-launch, writes /plan-launch in a message, or requests the complete launch planning workflow.
---

# Plan Launch

Adapted from [phuryn/pm-skills plan-launch](https://github.com/phuryn/pm-skills/blob/8607e3b077817f89bf4a9b623246219734ac3be0/pm-go-to-market/commands/plan-launch.md), under the [MIT license](../../pm-skills-LICENSE). This project-local Codex skill uses `$plan-launch`; it does not register a native slash command.

Use the user's language. Reuse project context and supplied PRDs or strategy. Clarify only missing launch essentials: subject, launch type and stage, existing users, timeline or hard deadlines, budget, and available people. If no launch subject can be inferred, ask for one. Preserve personal or noncommercial scope; do not introduce paid tiers, enterprise sales, or spending without a corresponding product decision.

## Workflow

1. Read and apply [beachhead-segment](../beachhead-segment/SKILL.md). Evaluate urgency, willingness to pay where applicable, reachable market share, and referral potential. Recommend one initial segment with rationale and possible adjacent segments.
2. Read and apply [ideal-customer-profile](../ideal-customer-profile/SKILL.md). Define relevant demographics, behaviors, discovery or buying process, JTBD, current alternatives, and qualification signals. For consumer products use individual user attributes instead of forcing company-size or procurement fields.
3. Read and apply [gtm-strategy](../gtm-strategy/SKILL.md). Develop positioning, audience-specific messaging, ranked channels, pre-launch and launch tactics, post-launch activity, pricing alignment if applicable, and success metrics.
4. Compile the launch plan with the sections below.

## Deliverable

Include target launch date and launch type, then:

- Beachhead segment: who, why first, and market size only when supported or explicitly estimated.
- Ideal customer profile: attributes, JTBD, current solution, and qualification signals.
- Positioning statement and messages by audience, with evidence for proof points.
- Channel table: tactic, expected reach, cost, and priority.
- Timeline: pre-launch, launch week, and post-launch actions, timing, and owners where known.
- Success metrics with proposed 30-day and 90-day targets.
- Risks with likelihood, impact, and mitigation.
- Conditional expansion plan after the initial segment is validated.

Label assumptions, cost estimates, targets, and unknowns. Use customer language. Match tactics to actual budget and staffing; do not represent estimated reach or returns as established facts.

Save Markdown in the established documentation location, defaulting to `docs/launch/<topic>.md`. Check for an existing plan and preserve unrelated content. Link the result. Planning does not authorize publishing, outreach, purchases, campaigns, or launching anything. Offer growth loops, battlecards, marketing drafts, or a metrics dashboard only as relevant next steps.
