# Dependency lockfile

Vendored (checked into `third_party/`, not fetched at build time):

| Dependency | Pinned version | Source | SHA256 of vendored file |
|---|---|---|---|
| nlohmann/json single header | v3.11.3 | https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp | see below |

System packages (not vendored; resolved via `pkg-config` at configure time —
pin your OS package manager / Dockerfile if bit-for-bit reproducibility
across machines matters):

| Package | Version used in EVIDENCE.md | apt package name |
|---|---|---|
| libavformat | 58.76.100 | libavformat-dev |
| libavcodec | 58.134.102 | libavcodec-dev |
| libavutil | 56.70.100 | libavutil-dev |
| libswscale | 5.9.100 | libswscale-dev |

Regenerate the SHA256 for the vendored header with:
```
sha256sum third_party/json.hpp
```
9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6  third_party/json.hpp
