# Fork Workflow Policy

## Overview

This fork intentionally does not use GitHub Actions. All workflow files (`.github/workflows/` and `.github/actions/`) have been removed to prevent accidental job execution.

## Why workflows are removed

- Avoid consuming GitHub Actions minutes in this fork.
- Avoid accidental release, Docker, package, website, security-scan, stale-bot, or model-triage jobs.
- Keep validation explicit and local to the developer or hardware test environment.
- No need to preserve upstream workflow files - they can always be fetched from upstream for reference.

## Local validation

Use local tooling before proposing or publishing changes:

| Task                           | Command                                      |
| ------------------------------ | -------------------------------------------- |
| Format                         | `trunk fmt`                                  |
| Lint/check changed files       | `trunk check`                                |
| Run native firmware tests      | `pio test -e native`                         |
| Run focused Docker native test | `./bin/test-native-docker.sh -f <test_name>` |
| Check T-Echo InkHUD build      | `pio test -e t-echo-inkhud`                  |
| Check T-Beam S3 Core build     | `pio test -e tbeam-s3-core`                  |
| Build a firmware target        | `pio run -e <env>`                           |
| Run hardware test harness      | `./mcp-server/run-tests.sh`                  |

Pick the smallest command set that covers the files and behavior changed. Shared firmware logic usually needs native tests plus at least one affected hardware target.

## Re-enabling a workflow

If you need to run CI:

1. Fetch the workflow files from upstream (`git fetch upstream`).
2. Copy the specific workflow you need into `.github/workflows/`.
3. Audit all triggers, repository guards, permissions, secrets, runners, cache keys, artifact retention, and publish destinations.
4. Remove fork-specific assumptions that should not apply to the target repository.
5. Run `trunk fmt` and `trunk check` after editing workflow files.
6. Document the reason in `CHANGELOG_FORK.md`.

Do not bulk-restore the upstream workflow directory. Only restore what you need, after review.
