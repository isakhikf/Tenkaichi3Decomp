#!/usr/bin/env python3
"""Fail on JSON/header drift, stale SQLite, or recipe ID/order mismatch."""
from pathlib import Path
import importlib.util
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('defense_build', ROOT/'tools/defense_db/build.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


def main():
    db = build.load(ROOT/'bt3_defesas_db.json')
    expected, sha = build.render(db)
    assert (ROOT/'port/src/ude_db_tab.h').read_text(encoding='utf-8') == expected, 'JSON/header drift'
    h = (ROOT/'port/src/ude_db.h').read_text()
    enum = h[h.index('enum UdeDbRecipe'):h.index('UDE_DB_RECIPE_COUNT')]
    names = re.findall(r'\bUDE_DB_([A-Z_]+)\b', enum)
    assert names == ['NONE'] + [r['id'] for r in db['tecnicas']], names
    build.check_sqlite(ROOT/'bt3_combat_database.sqlite', db, sha)
    # A meaningful drift probe: changing an extracted attack or a safety default
    # must change the generated header even if a caller forgot to regenerate it.
    db['attack_records'][0]['flags'] ^= 1
    changed, _ = build.render(db)
    assert changed != expected, 'attack mutation was ignored'
    db['attack_records'][0]['flags'] ^= 1
    db['ude_db1_policy']['defaults']['tap_lead100'] += 1
    changed, _ = build.render(db)
    assert changed != expected, 'configuration mutation was ignored'
    print('PASS generated tables, SQLite content, 21 recipe IDs, two drift probes')


if __name__ == '__main__':
    main()
