---
name: kernel-milestone
description: Checklist for syncing README.md and the ../minic-os-docs site after a minic-os kernel milestone ships. Only run this when the user explicitly asks to sync/ship docs for a milestone - do NOT invoke this proactively right after implementing a feature, even though CLAUDE.md's general workflow describes doc sync as part of "shipping". Docs in this project are deliberately batched, not done every session, per explicit user instruction.
---

# Syncing docs after a minic-os milestone

Only do any of this when the user explicitly asks for it (e.g. "sync the
docs", "close the docs backlog", "document the last N commits"). Implementing
a milestone does not by itself mean docs should be touched in the same turn.

## 1. Update `README.md` in this repo

- **"Current status"**: add a paragraph in the same prose style as the
  existing ones - describe what genuinely works now, and specifically call
  out how it was verified (exact values, a real round trip, a before/after
  count) rather than just naming the feature. Match the register: dense,
  technical, no marketing language, no bullet-fragment feature lists.
- **"Project layout"**: if the milestone added new files/directories, add
  them to the fenced tree block with the same one-line-per-file annotation
  style already used there.
- **"Known limitations"**: if the milestone deliberately scoped something
  down (a fixed table size, only-one-direction support, a missing edge
  case), add a bullet here in the same "on purpose, for now" tone the
  existing bullets use - this project treats a documented limitation as a
  first-class part of shipping, not an embarrassment to hide.

## 2. Sync `../minic-os-docs`

Check it's actually present as a sibling directory first (`ls ../minic-os-docs`).
That repo's own git history follows a one-commit-per-milestone pattern
mirroring this repo's own commits (e.g. this repo's "Add a real graphics
framebuffer" paired with `minic-os-docs`'s "Document the real graphics
framebuffer"). To find the sync gap:

```bash
git -C ../minic-os-docs log --oneline -20
git log --oneline -20
```

Compare the two logs - any commit in this repo without a matching
"Document ..." commit in `minic-os-docs` is undocumented. For each gap,
add real captured verification output (not just a feature description) to
the relevant page(s) - likely `reference.html` (architecture), `roadmap.html`
(capabilities overview), or `examples.html` (annotated session walkthrough)
- following that page's existing structure and depth. Commit each
milestone's doc update as its own "Document X" commit in `minic-os-docs`,
matching the existing one-to-one pattern, rather than one giant catch-up
commit.

## 3. Leave `CLAUDE.md` alone unless the milestone changed something structural

`CLAUDE.md`'s "Architecture notes worth knowing before touching this"
section is for hard, costly-to-rediscover constraints (a relocation
limitation, a reset-semantics gotcha, an entry-point requirement) - not a
running log of features. Only add to it if the milestone introduced a new
constraint of that kind; otherwise leave it untouched.

## 4. Confirm before reporting done

After editing, diff what changed (`git diff` in both repos) and read it
back once - the point of this checklist is that doc debt doesn't
silently reappear a few milestones later.
