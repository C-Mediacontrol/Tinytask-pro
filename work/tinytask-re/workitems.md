# Work Items

| ID | title | role | targets | surface | status | evidence | notes |
|----|-------|------|---------|---------|--------|----------|-------|
| WI-001 | Establish scope and auth | lead | case | process | completed | scope.md | offline-sample |
| WI-002 | PE Triage & Import Analysis | cre | tinytask.exe | PE/IAT | completed | E-triage.md | clean IAT, x86 PE32 |
| WI-003 | Static Analysis (Disassembly & Logic Recovery) | cre | tinytask.exe | .text/.data | completed | E-static.md | record/playback/file-format |
| WI-004 | Native Win32 C Implementation (<50KB) | cre | source code | Win32 C | completed | bin/tinytask.exe | pure C, 28,160 bytes |
| WI-005 | Verification, Testing & Deliverables | cre | build & report | quality | completed | report/ | verified 100% .rec format |

## Coverage
- [x] Recon/analysis complete for in_scope assets
- [x] Critical/High candidates triaged (or N/A for pure RE)
- [x] Validated findings have Evidence (E-*)
- [x] Path documented (attack/call/solve)
- [x] Timeline continuous across major phases
- [x] Report via docs-generator
- [x] field-journal anonymized

## Refs
- skills/ops/timeline-workitem.md
- skills/ops/evidence-finding-path.md