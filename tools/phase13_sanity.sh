#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

ok(){ printf "[OK] %s\n" "$1"; }
warn(){ printf "[WARN] %s\n" "$1"; }
fail(){ printf "[FAIL] %s\n" "$1"; exit 1; }

python -m json.tool plugin.json >/dev/null || fail "plugin.json is invalid JSON"
ok "plugin.json is valid JSON"

plugin_slug=$(python - <<'PY'
import json
with open('plugin.json') as f:
    j=json.load(f)
print(j.get('slug',''))
PY
)
plugin_name=$(python - <<'PY'
import json
with open('plugin.json') as f:
    j=json.load(f)
print(j.get('name',''))
PY
)
[[ "$plugin_slug" == "Sticksy" && "$plugin_name" == "Sticksy" ]] || fail "plugin slug/name are not Sticksy"
ok "plugin slug/name are consistent with Sticksy"

for slug in SticksyBlank3 SticksyBlank5 SticksyBlank9 SticksyBlank12; do
  python - <<PY >/dev/null || fail "module slug missing from plugin.json: $slug"
import json
with open('plugin.json') as f:
    mods=json.load(f).get('modules',[])
assert any(m.get('slug')=='$slug' for m in mods)
PY
  rg -n "createModel<.*>\(\"$slug\"\)" src/plugin.cpp >/dev/null || fail "module model slug not found in src/plugin.cpp: $slug"
done
ok "module slugs are consistent"

for hp in 3hp 5hp 9hp 12hp; do
  [[ -d "res/panels/$hp" ]] || fail "missing panel folder: res/panels/$hp"
  for bg in neutral wood metal fabric paper black white; do
    [[ -f "res/panels/$hp/$bg.svg" ]] || fail "missing panel asset: res/panels/$hp/$bg.svg"
  done
done
ok "all four module panel paths and backgrounds exist"

[[ -f res/fallback/Sticksy.svg ]] || fail "fallback asset missing: res/fallback/Sticksy.svg"
ok "fallback path exists"

[[ -f README.md ]] || fail "README.md missing"
ok "README exists"

junk=$(find . -maxdepth 4 -type f \( -name '*.tmp' -o -name '*.log' -o -name '.DS_Store' -o -name 'Thumbs.db' -o -name '*~' \) | sed 's#^./##' || true)
if [[ -n "$junk" ]]; then
  fail "temporary/generated junk files found:\n$junk"
fi
ok "no temporary/generated junk files found"

rg -n "printf\(|std::cout|std::cerr|OutputDebugString" src >/dev/null && fail "debug print usage found" || ok "no debug prints found"

rg -n "/Users/|/home/|[A-Za-z]:\\\\" src plugin.json README.md Makefile >/dev/null && fail "hardcoded local-machine path found" || ok "no hardcoded local-machine paths found"

rg -n "\\\\" src/plugin.cpp >/dev/null && warn "backslashes present in source; review if path-related" || ok "no Windows-only path assumptions detected"

if make >/tmp/sticksy_make.out 2>/tmp/sticksy_make.err; then
  ok "make succeeded"
else
  if rg -n "plugin.mk: No such file or directory" /tmp/sticksy_make.err >/dev/null; then
    warn "make not runnable: Rack SDK missing at configured RACK_DIR"
  else
    cat /tmp/sticksy_make.err
    fail "make failed"
  fi
fi

if make dist >/tmp/sticksy_dist.out 2>/tmp/sticksy_dist.err; then
  ok "make dist succeeded"
else
  if rg -n "plugin.mk: No such file or directory" /tmp/sticksy_dist.err >/dev/null; then
    warn "make dist not runnable: Rack SDK missing at configured RACK_DIR"
  else
    cat /tmp/sticksy_dist.err
    fail "make dist failed"
  fi
fi

ok "Phase 13 sanity checks complete"
