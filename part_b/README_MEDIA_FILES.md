# Regenerating the Part B media files

`source_master.mp4`, `proxy_allintra.mp4`, and `proxy_shortgop.mp4` are
**not** included in this archive/repo — combined they're ~266MB of
synthetic (regeneratable) video, which doesn't belong in a git history or a
download.

Regenerate them with:
```bash
./scripts/generate_proxies.sh
```
This is deterministic (same lavfi Mandelbrot source, same encode settings)
and takes roughly 2 minutes on modest hardware. See `EVIDENCE_PART_B.md` §1
for the exact byte sizes / bitrates you should see, and `DECISIONS_PART_B.md`
for why the settings (30s duration, `end_scale=0.02`) are what they are.

`allintra_report.json` and `shortgop_report.json` **are** included — those
are the actual small (~100KB / ~9KB) JSON evidence artifacts the benchmark
tables in `EVIDENCE_PART_B.md` were generated from.
