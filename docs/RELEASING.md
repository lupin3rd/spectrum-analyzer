# Releasing

Two repositories, two different jobs.

| Remote | URL | Contents |
|---|---|---|
| `gitea` | `https://git.franceschini.it/lupin3rd/spectrum-analyzer.git` | **Development repository. Everything**: full history, every branch, and `agent/` with the coding-agent transcripts. This is where work happens. |
| `github` | `git@github.com:lupin3rd/spectrum-analyzer.git` | **Release repository. Release commits only.** No `agent/`, no build output, no development history. |

## The rule

GitHub receives **one commit per release**, authored and committed by the project
owner:

```
lupin3rd <1455316+lupin3rd@users.noreply.github.com>
```

The tree of that commit is the development tree **minus the directories that
exist only for the local repository** (`agent/`). Nothing else is filtered:
`docs/`, `tests/`, `packaging/`, `vendor/` and `.github/` all ship, because they
are needed to build, test and release.

Consequences to keep in mind:

- `gitea/main` is normally **ahead of** `github/main`. That is the point: the
  public history has one commit per release and does not show the work in
  progress.
- The same version tag points at **different commits** in the two repositories:
  `v1.1.0` is the merge commit on Gitea and the release commit on GitHub. Do not
  compare them.
- Anything added to the public tree has to go through a release. A typo fix in
  the public README is a new release.

## Procedure

1. Work on a branch, open a pull request on Gitea, merge it. (`git push` to
   `main`/`master` is blocked by the agent guardrails; the merge goes through the
   Gitea API.)
2. Bump `version.h`, add the `CHANGELOG.md` entry, commit.
3. Create the public release commit:

   ```sh
   ./scripts/build-public-release.sh HEAD <parent-release-commit> /tmp/message.txt
   ```

   `<parent-release-commit>` is the previous release commit on GitHub — **not**
   the development `HEAD`. It prints the new commit sha.
4. Make GitHub point at it. The first time the objects must be uploaded, and the
   guardrail blocks `git push` on a protected branch, so the ref is moved through
   the API:

   ```sh
   git push github <sha>:refs/heads/tmp-release
   gh api -X PATCH repos/lupin3rd/spectrum-analyzer/git/refs/heads/main \
       -f sha=<sha> -F force=true
   gh api -X DELETE repos/lupin3rd/spectrum-analyzer/git/refs/heads/tmp-release
   ```

5. Tag and push the tag:

   ```sh
   git tag -a v<version> <sha> -m "Spectrum Analyzer <version>"
   git push github v<version>
   ```

6. CI does the rest: the `build` workflow runs the tests on Linux and Windows,
   produces the AppImage and the Windows archive, and creates the GitHub release
   with both; the `macos` workflow builds the `.app` and attaches it to the same
   release. Then set the release notes:

   ```sh
   gh release edit v<version> --title "..." --notes-file notes.md
   ```

7. Keep the Gitea tag pointing at the development commit (it is created from
   there); do not force it to the release commit.

## What a release tree must not contain

- `agent/` — session transcripts, sync script, agent README.
- Build output: `*.o`, `*.d`, `spectrum-analyzer*` binaries, `tests/*-test`,
  `*.AppImage`, `*.AppDir`, `.cache/`. All of these are in `.gitignore`; a
  release commit built with `scripts/build-public-release.sh` inherits that.
- Anything else generated: check with `git ls-tree -r --name-only <sha>` before
  pushing.

## Note on history rewrites

Rewriting a ref on GitHub does not delete the objects already received: an
orphaned commit stays reachable by its SHA. If something that must not be public
was ever pushed, rewriting the branch is not enough — ask GitHub support to
purge it.
