"""Manual fix for remaining ）? corruptions based on context analysis."""
import re, os

ROOT = r"e:\Vista\Pictures\Temp\research_compitition\Project_Prometheus_Tasks"

FIXES = {}

# task-02: 5 remaining
for pos, ch in [(3053," "),(4755,"\u7528"),(9420,"\u4e2d"),(9477,"\u4e0e"),(9681,"\u7528")]:
    FIXES[("task-02-drone-sitl.md",pos)] = ch

# task-04: 2 remaining
for pos, ch in [(16386,"\u5230"),(16457,"\u6216")]:
    FIXES[("task-04-mecanum-chassis.md",pos)] = ch

# task-05: 2 remaining
for pos, ch in [(12016,"\u2192 "),(14307,"\u548c")]:
    FIXES[("task-05-sensors.md",pos)] = ch

# task-06: 14 remaining
t6 = "task-06-com-bridge.md"
for pos, ch in [(197," "),(241,"\u2502"),(8311,"\u2192 "),(8331,"\u2192 "),(8393,"\u2192 "),
    (12918,"\u2192 "),(13451,"\u2192 "),(13848,"\u2192 "),(14059,"\u7528"),
    (14303,"\u2192 "),(14643,"\u2192 "),(14652,"\u2192 "),(14659,"\u2192 "),(15197,"\u2192 ")]:
    FIXES[(t6,pos)] = ch

# task-09: 3 remaining
t9 = "task-09-validation.md"
for pos, ch in [(5607,"\u2192 "),(7757,"\u2014 "),(8822,"\uff09")]:
    FIXES[(t9,pos)] = ch

# task-07: 40 remaining
t7 = "task-07-edge-server.md"
t7_positions = [1997,5537,8568,8608,9342,10323,11294,11305,12880,
    15548,15567,15581,16070,16111,16672,17285,17345,
    19766,20293,20405,20419,21549,22616,22842,23027,
    23207,23518,23535,23550,23796,24847,25081,25355,
    25383,26508,27197,27615,28918,28934,31710]
t7_special = {11294:"\u89e6",11305:"\uff0c",15548:"\u4e8c",15567:"\u7684",
    15581:"\u7684",23207:"\u4e8c",25383:"\u5c06"}
for pos in t7_positions:
    FIXES[(t7,pos)] = t7_special.get(pos, "\u2192 ")

# Apply
fixed = 0
nf = 0
for (fname, target_pos), replacement in FIXES.items():
    fpath = os.path.join(ROOT, fname)
    with open(fpath, "r", encoding="utf-8") as f:
        text = f.read()
    found = False
    for m in re.finditer(r"\uff09\?", text):
        if m.start() == target_pos:
            new_text = text[:m.start()] + replacement + text[m.end():]
            with open(fpath, "w", encoding="utf-8") as f:
                f.write(new_text)
            fixed += 1
            found = True
            break
    if not found:
        nf += 1

print(f"Fixed={fixed} NotFound={nf}")

for fname in set(f[0] for f in FIXES):
    fpath = os.path.join(ROOT, fname)
    with open(fpath, "r", encoding="utf-8") as f:
        text = f.read()
    r = text.count("\uff09?")
    print(f"  {fname}: {r} remaining" if r else f"  {fname}: CLEAN")
