"""Analyze citations vs bibliography in main.tex."""
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')

fp = Path(__file__).resolve().parents[1] / 'main.tex'
text = fp.read_text(encoding='utf-8')

pos = text.find('Список использованных источников')
body = text[:pos]
biblio = text[pos:]

# Citations like [12] or [3] in body
cites = re.findall(r'\[(\d{1,2})\]', body)
counts = {}
for c in cites:
    counts[int(c)] = counts.get(int(c), 0) + 1

# Bibliography items: lines starting with \item
ITEM_PAT = re.compile(r'(?m)^\\item\s+(.{0,140})')
items = ITEM_PAT.findall(biblio)

print(f'Body citations: {len(cites)} total, {len(set(int(c) for c in cites))} unique')
print(f'Bibliography entries: {len(items)}')
print()
print('=== Cited ===')
for n in sorted(counts):
    if n <= len(items):
        print(f'  [{n:>2}] x{counts[n]}: {items[n-1][:100]}')
print()
unused = [i for i in range(1, len(items) + 1) if i not in counts]
print(f'=== Uncited ({len(unused)}) ===')
for n in unused:
    print(f'  [{n:>2}]: {items[n-1][:120]}')
