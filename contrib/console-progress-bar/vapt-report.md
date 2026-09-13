# VAPT Report

> Note added after the scan: the scanned commit `4f40ea87` was later rewritten into
> `37b4e97f` and its successors (commit message and README text only; every source,
> test and manual file is byte for byte the one scanned). A local file path in the
> header was replaced by `aria2 (fork checkout)`.

## Executive Summary

- **Repository:** aria2 (fork checkout)
- **Branch:** feature/console-progress-bar (HEAD 4f40ea87)
- **Baseline:** origin/master (merge-base 9e7273583f)
- **Review scope:** changed — 15 files (15 committed / 0 staged / 0 unstaged / 0 untracked); 1 unpushed commit `4f40ea87 Add a progress bar to the console readout`
- **Date:** 2026-09-13 15:21 IST
- **Overall result:** PASS
- **One-paragraph verdict:** Nothing blocks the push. The commit adds a console progress bar (new `src/ProgressBar.cc/.h`, UTF-8 aware truncation in `src/ColorizedStream.cc`, four new `--progress-bar*` options, docs, tests). Semgrep raised one HIGH that is a false positive: a JavaScript rule matched the word `ws://` in a documentation paragraph from 2012 that the commit did not touch. Gitleaks found no secrets in the tree or in the commit. OSV-Scanner found no package manifests (C++ project; dependencies are system libraries). The manual review found no untrusted input reaching a dangerous sink: every new input is a CLI/config option validated against a fixed list or a 0–200 number range, none of the new options can be changed over RPC, all divisions are guarded, and all buffer writes are bounded. CodeQL was not run (opt-in, deferred by the user); run `/vapt --codeql cpp` if you want the deep pass before pushing.

## Risk Summary

| Severity | Confirmed (TRUE/LIKELY) | Uncertain | Rejected (FP) |
|---|---|---|---|
| CRITICAL | 0 | 0 | 0 |
| HIGH | 0 | 0 | 1 |
| MEDIUM | 0 | 0 | 0 |
| LOW | 0 | 0 | 0 |
| INFO | 0 | 0 | 0 |

## Blocking Findings

None.

## Other Findings

None.

### AI review evidence (what was checked, all clean)

| Area | File:line | What could go wrong | Why it does not |
|---|---|---|---|
| Option input | src/OptionHandlerFactory.cc:264-292 | bad style/colour/width value | `ParameterOptionHandler` accepts only the 9 style names and 16 colour names; `NumberOptionHandler` clamps width to 0..200; unknown style name → AUTO, unknown colour → clear |
| RPC surface | src/OptionHandlerFactory.cc:264-292, src/RpcMethodImpl.cc:1147-1153 | remote client changes the new options | `changeGlobalOption` only accepts handlers with `setChangeGlobalOption(true)`; the four new handlers never set it, so they are start-up only |
| Table index | src/ProgressBar.cc:107-110 (`charsFor`) | out-of-bounds read of `STYLE_TABLE` | `enum class ProgressBarStyle` has 9 values, the table has 9 rows; only enum values from `toProgressBarStyle`/`resolveProgressBarStyle` reach it |
| Env parsing | src/ProgressBar.cc:113-123 (`namesUTF8`) | read past the end of `LC_ALL`/`LC_CTYPE`/`LANG` | each `s[n+1]` is read only after `s[n]` matched a letter, so the NUL terminator stops the scan; the value is only compared, never executed or written |
| Division | src/ConsoleStatCalc.cc:203-208, 308-311, 391-405, 420-428 | divide by zero | `getTotalLength() > 0`, `op.total > 1` and `p.done == 0` guards sit in front of every `/` |
| Bar buffer | src/ProgressBar.cc:198-245 (`progressBar`) | over-long output | `frac` clamped to [0,1]; `width` ≤ min(200, terminal columns); output is exactly `width` columns (test `testBarIsAlwaysExactlyTheRequestedWidth`) |
| Truncation | src/ColorizedStream.cc:76-105, 118-121 | write past the string when cutting at N columns | `prefixBytesForColumns` returns a byte count ≤ `s.size()`; `rv.write` uses that count |
| Terminal size | src/ConsoleStatCalc.cc:441-457 | width 0 or negative | `ws_col > 1` and `dwSize.X > 2` guards; default stays 79 |
| Windows TTY | src/ConsoleStatCalc.cc:334-345, 354 | wrong console detection | `GetConsoleMode` on the real stdout handle; fails cleanly for pipes and files |
| Lifetime | src/ConsoleStatCalc.h:118-124, src/MultiUrlRequestInfo.cc:143-149 | dangling `barColor_` pointer | it points at `colors::*` globals with static lifetime |

Compile check: `../aria2-build/out/aria2c.exe` (2026-09-13 12:26) was built from this commit, so the code builds.

## False Positives

- **[VAPT-001] semgrep `javascript.lang.security.detect-insecure-websocket`** — `doc/manual-src/en/aria2c.rst:2371` (pre-existing, not in diff). Scanner rating: HIGH (ERROR), confidence LOW. The line is prose in the manual: "The WebSocket URI for JSON-RPC over WebSocket is ``ws://HOST:PORT/jsonrpc``. If you enabled SSL/TLS encryption, use ``wss://HOST:PORT/jsonrpc`` instead." It is documentation, not JavaScript, it already tells the reader to use `wss://` with TLS, it was written in 2012 (`git blame` → bc3c553b3), and the only hunk this commit adds to the file is `@@ -1338,0 +1339,51 @@` (the new option docs). No attacker input, no sink. Rejected.

## Scanner Coverage

| Scanner | Status | Version | Findings | Note |
|---|---|---|---|---|
| Semgrep | RUN | 1.177.0 | 1 (rejected) | configs p/default, p/security-audit, p/secrets; 15 files in scope |
| Gitleaks | RUN | 8.30.1 | 0 | working tree (`gitleaks dir`) + commit range 9e7273583f..HEAD (1 commit, 31.87 KB); "no leaks found" |
| OSV-Scanner | RUN | 2.5.1 | 0 | 168 dirs visited, no package manifests or lockfiles found (C++/autotools project) |
| CodeQL | NOT RUN | 2.27.0 installed | — | not requested (opt-in; user chose to run it later). An opt-in scanner that was not requested does not lower the verdict. |
| AI review | RUN | Claude Opus 5 | 0 | diff.patch (1207 lines) + callers/callees in RequestGroupMan.h, NetStat.h, TimerA2.cc, ColorizedStream.h, RpcMethodImpl.cc, OptionHandler.h |

## Test results

- Not run (no `--tests`). The commit adds `test/ProgressBarTest.cc` (16 cppunit cases); run them with the project's `make check` inside the build container.
- Test failures are not security findings.

## Limitations

- CodeQL (data-flow analysis for C++) was not run. Run `/vapt --codeql cpp --codeql-build "<your build command>"` for it; CodeQL for C++ needs a working build, which here lives in the Docker/MinGW setup under `../aria2-build/`.
- OSV-Scanner had nothing to read: aria2's dependencies (OpenSSL/GnuTLS, libssh2, zlib, c-ares, libxml2/expat, sqlite3, cppunit) are system libraries chosen at build time, not listed in a manifest inside the repo. Their versions are pinned in `../aria2-build/Dockerfile.deps`, which is outside the git repo and was not scanned.
- Semgrep scanned the whole of each changed file, not only the changed lines; that is why it found a 2012 documentation line.
- Binary/build artefacts were not analysed. Runtime behaviour was not executed as part of this audit.

## Next steps

1. Nothing to fix. You can push this commit.
2. No secret was found; nothing to rotate.
3. Optional deep pass before pushing: `/vapt --codeql cpp` (5–30 min, needs the build).
