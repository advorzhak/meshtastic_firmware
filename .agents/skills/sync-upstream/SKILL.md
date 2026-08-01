---
name: sync-upstream
description: >
  Full upstream sync workflow: fetch → rebase develop onto upstream/develop
  (resolving all conflicts) → trunk autoupdate → trunk fmt --all → trunk
  check --all (resolving all findings) → build verify t-echo / t-echo-inkhud /
  tbeam-s3-core → amend last commit → force push with lease. Load this skill
  whenever the user asks to sync with upstream, rebase onto upstream/develop,
  or run the full lint-and-push workflow.
---

# Skill: sync-upstream

Execute the full upstream-sync pipeline for this fork of `meshtastic/firmware`.
Work through every phase in order. Never skip a phase. Never ask the user for
confirmation between phases unless an unrecoverable situation is reached.

## Conventions

- Bash blocks run from the repo root. Execute them with your shell tool.
- Prefer native file tools over shell equivalents: read files with your file
  reader (not `cat`), edit with your file editor (not `sed`/`awk`), search
  with your search tool (not shell `grep`) - except where a phase gives an
  explicit shell command.
- Track the phases with your todo/task tool: one item per phase, exactly one
  in progress at a time, marked done immediately on completion.
- "Read before edit": never edit a file you haven't read in this session.

---

## Step 0 - Create the todo list

Register one todo item per phase before doing any work:

- Phase 0: Preconditions
- Phase 1: Fetch upstream
- Phase 2: Rebase onto upstream/develop
- Phase 3: trunk upgrade
- Phase 4: trunk fmt --all
- Phase 5: Address fmt findings
- Phase 6: trunk check --all + fix all findings
- Phase 7: Submodule sync
- Phase 8: Build verify (t-echo, t-echo-inkhud, tbeam-s3-core)
- Phase 9: Amend last commit
- Phase 10: Force push with lease
- Phase 11: Summary

---

## Phase 0 - Preconditions

```bash
git branch --show-current && echo "---" && git remote -v && echo "---" && git status --short
```

- Branch must be `develop`. If not, stop and report.
- `upstream` remote must exist. If missing:
  ```bash
  git remote add upstream https://github.com/meshtastic/firmware.git
  ```
- Working tree must be clean. If dirty:
  - IDE config files (`.vscode/`, `.idea/`): auto-discard with `git restore <file>`
  - Tracked source files: stop and report to the user
  - **Never stash silently.**

---

## Phase 1 - Fetch

```bash
git fetch upstream 2>&1
```

Show divergence:

```bash
echo "=== upstream ahead ===" && git log --oneline develop..upstream/develop | wc -l && \
echo "=== develop ahead ===" && git log --oneline upstream/develop..develop && \
echo "=== file diff stat ===" && git diff --stat develop...upstream/develop | tail -6
```

Report commit counts before proceeding.

---

## Phase 2 - Rebase

```bash
git rebase upstream/develop 2>&1
```

### No conflicts

Verify alignment:

```bash
echo "Your commits:" && git log --oneline upstream/develop..develop
echo "Behind upstream (must be empty):" && git log --oneline develop..upstream/develop
```

### Conflicts

Work this loop for each conflicting commit:

**2a - Identify conflicted files**

```bash
git status --short
```

Files marked `UU` (both modified) or `DD`/`AU`/`UA` need resolution.

**2b - Read each conflicted file**

Read the full file into context before touching it - do not rely on
`git diff` output alone. Locate the conflict markers precisely (search for
`<<<<<<<`).

**2c - Resolve the conflict**

Edit the file, replacing the entire conflict block (`<<<<<<<` through
`>>>>>>>`) with the correct merged result. Follow the strategy table:

| File type                                                          | Strategy                                                                                                                                                                                                                               |
| ------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/**/*.cpp`, `src/**/*.h`                                       | Preserve upstream API/type changes; layer your logic on top. Never drop either side silently.                                                                                                                                          |
| `platformio.ini`                                                   | Upstream wins on versions. Append your env additions below upstream's.                                                                                                                                                                 |
| `protobufs/` submodule pointer                                     | **Do not edit.** Use the escape hatch: abort → regen → restart.                                                                                                                                                                        |
| `src/mesh/generated/**`                                            | Same - never hand-edit generated code.                                                                                                                                                                                                 |
| `variants/**`                                                      | Upstream wins for upstream-owned variants. Keep custom variants.                                                                                                                                                                       |
| `.github/workflows/**`                                             | **Always disable**: rename `.yml` → `.yml.disabled`. For modify/delete conflicts (our fork deleted, upstream modified), keep the deletion - do not restore the upstream version. Never restore unless documented in CHANGELOG_FORK.md. |
| `userPrefs.jsonc`                                                  | Our version always wins.                                                                                                                                                                                                               |
| `.trunk/trunk.yaml`                                                | Merge both sides' additions. Never downgrade upstream version bumps.                                                                                                                                                                   |
| `CHANGELOG_FORK.md`, `docs/ARCHITECTURE.md`, `docs/uml/**`         | Our files - always keep ours.                                                                                                                                                                                                          |
| `src/modules/BitChatBridgeModule.*`, `test/test_bitchat_bridge/**` | Our files - always keep ours.                                                                                                                                                                                                          |
| Any other file                                                     | Upstream wins for files it exclusively owns. Carefully merge files both sides touched.                                                                                                                                                 |

**2d - Verify no markers remain**

```bash
grep -rnE '^(<{7}|={7}|>{7})' src/ .github/ extra_scripts/ 2>/dev/null
```

If any match, return to 2b. (Markdown setext underlines of exactly 7 `=` can
false-positive; inspect before re-editing.)

**2e - Stage and continue**

```bash
git add <resolved-file>
GIT_EDITOR=true git rebase --continue 2>&1
```

Repeat until the rebase completes.

#### Escape hatches

| Situation               | Action                                                                 |
| ----------------------- | ---------------------------------------------------------------------- |
| Submodule conflict      | `git rebase --abort` → `bin/regen-protos.sh` → commit → restart rebase |
| Generated file conflict | Same as above                                                          |
| Rebase unrecoverable    | `git rebase --abort` - report to the user and **stop**                 |

### Post-rebase checks

```bash
git submodule update --init 2>&1
git status --short
```

If `protobufs` is dirty after sync, note it - handled in Phase 7.

Disable any new workflows brought in by upstream:

```bash
find .github/workflows -maxdepth 1 -name "*.yml" -not -name "*.disabled" -exec mv {} {}.disabled \;
```

---

## Phase 3 - trunk upgrade

```bash
trunk upgrade 2>&1
```

Check if `trunk.yaml` changed:

```bash
git diff .trunk/trunk.yaml
```

If changed, read it to confirm versions are coherent. If trunk upgrade reported "Already up to date", no changes are needed. Non-blocking - continue even if some linters fail to upgrade.

---

## Phase 4 - trunk fmt --all

```bash
trunk fmt --all --no-progress 2>&1
```

Check for modified tracked files:

```bash
git diff --name-only
```

---

## Phase 5 - Address fmt findings

`trunk fmt` may surface issues it can't auto-fix. For each:

1. Read the file.
2. Apply the fix.
3. Verify: `trunk check --no-progress --show-existing <file> 2>&1`
4. For false positives, add the narrowest `trunk-ignore` comment with a
   reason string, then re-run step 3 to confirm suppression.

---

## Phase 6 - trunk check --all

```bash
trunk check --all --no-progress 2>&1
```

### Triage

| Category                  | Signal                                                               | Action                                             |
| ------------------------- | -------------------------------------------------------------------- | -------------------------------------------------- |
| Real bug / security issue | Logic error, injection risk, hardcoded secret                        | Fix the code                                       |
| False positive            | Entropy detector on public value, root-user in intentional container | Add narrowest `trunk-ignore`                       |
| Stale `trunk-ignore`      | `trunk/ignore-does-nothing` or `trunk/ignore-disabled-linter`        | Remove the comment                                 |
| Formatting leftover       | `fmt` issue remains after Phase 4                                    | Re-run `trunk fmt <file>`                          |
| Tool install failure      | `FAILURE` + cargo/pip error in `.trunk/out/*.yaml`                   | Read the YAML, fix runtime version in `trunk.yaml` |

### Resolution rules

Read a file before editing it.

- **`ruff/F401` / `flake8/F401`** - unused import: remove it, or replace
  try/import availability checks with `importlib.util.find_spec`.
- **`actionlint/if-cond`** - `${{ }}` in `if:`: remove the wrapper.
- **`actionlint/workflow-call`** - undefined input in `with:`: remove the key.
- **`checkov/CKV_GHA_7`** - remove unused `inputs:` blocks from `workflow_dispatch`.
- **`yamllint/quoted-strings`** - remove redundant quotes (keep quotes for
  emojis, colons, `{`/`}`).
- **`trivy/DS-0002` / `checkov/CKV_DOCKER_8`** - root USER in build container:
  add file-level `trunk-ignore` with exact rule ID (`DS-0002`, not `DS002`).
- **`trivy/DS-0026` / `checkov/CKV_DOCKER_2`** - no HEALTHCHECK: same policy,
  exact rule ID (`DS-0026` not `DS026`).
- **`markdownlint/MD040`** - fenced block without language: add `text` or
  appropriate identifier to the opening fence.
- **`markdownlint/MD060`** - table column style: wrap with
  `<!-- markdownlint-disable MD060 -->` / `<!-- markdownlint-enable MD060 -->`.
- **`checkov/CKV_SECRET_6`** - high-entropy false positive: confirm the flagged
  line with `trunk check --show-existing <file>`, then add
  `// trunk-ignore(checkov/CKV_SECRET_6): <reason>` on the line before (JSONC)
  or inline (other types). Verify with another `--show-existing` run.
- **`trunk/ignore-does-nothing` / `trunk/ignore-disabled-linter`** - locate
  the stale comment, remove it.
- **Tool `FAILURE`** - read `.trunk/out/<failure>.yaml`, fix runtime version
  in `.trunk/trunk.yaml` (e.g. `rust@1.82.0` → `rust@1.85.0`), re-run.

### Per-file verification

After every fix:

```bash
trunk check --no-progress --show-existing <file> 2>&1
```

Must show no findings before moving to the next file.

### Completion criterion

Repeat the full `trunk check --all --no-progress` run until the output is:

```text
✔ No issues
```

The only tolerable non-issue output is a `FAILURE` for a tool that genuinely
cannot be installed on this platform (document in summary).

---

## Phase 7 - Submodule sync

```bash
git status --short
```

If `protobufs` is modified:

```bash
git submodule update --init protobufs 2>&1
git status --short
```

Verify all workflows are disabled:

```bash
if find .github/workflows -maxdepth 1 -name "*.yml" -not -name "*.disabled" 2>/dev/null | grep -q .; then
  echo "ERROR: Active workflows found!"
else
  echo "OK: All workflows disabled"
fi
```

Working tree must be clean before Phase 8.

---

## Phase 8 - Build verify

Build three canonical variants serially. PlatformIO doesn't parallelize envs.

```bash
mkdir -p .pio/build-verify
LOG=.pio/build-verify/verify.log
for env in t-echo t-echo-inkhud tbeam-s3-core; do
  echo "=== $env ===" >> "$LOG"
  pio run -e "$env" >> "$LOG" 2>&1
  echo "exit=$?  $env" >> "$LOG"
done
echo "--- summary ---"
grep "^exit=" "$LOG"
```

Each `exit=` line must be `exit=0`. If any non-zero:

- Extract firmware version from `version.properties` (INI format: `[VERSION]` section with `major`, `minor`, `build` keys - e.g. `2.8.0`) or from the built binary artifact for diagnostics.

- Run the failing env alone: `pio run -e <env>` to surface the compiler error.
- Common breakages: stale `protobufs` (rerun `bin/regen-protos.sh`), upstream
  API rename not propagated to fork modules, nRF52 warm-region guard failure,
  ESP32 partition overflow.
- Fix the code, re-run the full loop. Only complete when all three pass.
- **Never amend or push with a broken build.**

Clean up when done:

```bash
rm -rf .pio/build-verify
```

---

## Phase 9 - Amend last commit

If the working tree is clean after the rebase (no uncommitted changes), the amend is a no-op and the current HEAD SHA is used. Otherwise:

```bash
git add -A && git commit --amend --no-edit 2>&1
```

Capture the new SHA for the summary.

---

## Phase 10 - Force push with lease

```bash
git push origin develop --force-with-lease 2>&1
```

`--force-with-lease` is mandatory. Never use plain `--force` unless the user
explicitly asks. This push is pre-authorized as part of the workflow - do not
stop to ask for confirmation.

---

## Phase 11 - Summary

Print a structured report after the push completes. The report must include:

1. **Fork commits**: how many commits the fork now has on top of
   `upstream/develop` (not just the one synced commit), with full
   subject lines listed one per line.

2. **Sync details**: upstream commits merged, conflicts resolved,
   linters upgraded, files reformatted, build variants verified.

3. **Current state**: branch name, local SHA, upstream SHA, firmware version
   (major.minor.build from `version.properties`), build host, trunk version.

4. **Lint status**: result from `trunk check --all`.

5. **Push confirmation**: `origin/develop` forced with lease.

The overall sync is considered complete only when all of the above
metrics are reported and the push was acknowledged by the remote.

---

## Summary report (part of Phase 11)

After Phase 10, the Phase 11 summary above is printed. Additionally, confirm the push with:

```text
Phase 1  Fetch            upstream was N commits ahead
Phase 2  Rebase           N conflicts resolved (files: …) / clean
Phase 3  trunk upgrade    N linters bumped / no changes
Phase 4  trunk fmt        N files reformatted
Phase 5  fmt findings     N manual fixes applied
Phase 6  trunk check      N code fixes, N suppressions (reasons listed)
Phase 7  Submodule        clean / synced protobufs
Phase 8  Build verify     t-echo / t-echo-inkhud / tbeam-s3-core all succeeded
Phase 9  Amend            <new SHA>
Firmware version  major.minor.build from version.properties (e.g. 2.8.0)
Phase 10 Push             forced to origin/develop ✓
```

---

## House rules

- **Maintain the todo list** - create it at the start, keep it current.
- **Read before edit** - never edit a file you haven't read in this session.
- **Search, don't skim** - locate conflict markers and stale ignore comments
  with search instead of reading large files end-to-end.
- **Workflow disable enforcement** - always disable upstream workflows
  (`.yml` → `.yml.disabled`) after rebase.
- **Never `git stash`** - stashes are shared across worktrees.
- **Never abort the rebase silently** - always report first.
- **Never hand-edit generated protobuf files** - use `bin/regen-protos.sh`.
- **Never add a `trunk-ignore` without verifying** it suppresses the finding
  via `trunk check --show-existing <file>`.
- **Never use bare `--force`** - always `--force-with-lease`.
- **Never mark a phase complete** while it has open findings.
