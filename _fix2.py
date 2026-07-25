"""Final fix: replace remaining ）? by position with context-determined chars."""
import re, os

ROOT = r"e:\Vista\Pictures\Temp\research_compitition\Project_Prometheus_Tasks"

FIXES = {}

for p, c in [(4754,"\u7528"),(9419,"\u4e2d"),(9476,"\u4e0e"),(9680,"\u7528")]:
    FIXES[("task-02-drone-sitl.md",p)] = c

FIXES[("task-04-mecanum-chassis.md",16456)] = "\u6216"

t6 = "task-06-com-bridge.md"
for p, c in [(240,"\u2518"),(8310,"\u2192 "),(8330,"\u2192 "),(8392,"\u2192 "),
    (12917,"\u2192 "),(13450,"\u2192 "),(13847,"\u2192 "),(14058,"\u7528"),
    (14302,"\u2192 "),(14642,"\u2192 "),(14651,"\u2192 "),(14658,"\u2192 "),
    (15196,"\u2192 ")]:
    FIXES[(t6,p)] = c

t7 = "task-07-edge-server.md"
for p, c in [(11304,"\u89e6"),(12879,"\u7528"),(15547,"\u4e8c"),(15566,"\u7684"),
    (15580,"\u7684"),(16069,"\u2190 "),(16110,"\u2192 "),(16671,"\u2190 "),
    (17284,"\u2192 "),(17344,"\u2192 "),(19765,"\u2192 "),(20292,"\u2192 "),
    (20404,"\u2192 "),(20418,"\u2192 "),(21548,"\u2192 "),(22615,"\u2190 "),
    (22841,"\u2192 "),(23026,"\u2192 "),(23206,"\u4e8c"),(23517,"\u2192 "),
    (23534,"\u2192 "),(23549,"\u2192 "),(23795,"\u2192 "),(24846,"\u2192 "),
    (25080,"\u2192 "),(25354,"\u2192 "),(25382,"\u5c06"),(26507,"\u2192 "),
    (27196,"\u2192 "),(27614,"\u2192 "),(28917,"\u2192 "),(28933,"\u2192 "),
    (31709,"\u2192 ")]:
    FIXES[(t7,p)] = c

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

print(f"Applied={fixed} Missed={nf}")
for fname in sorted(set(f[0] for f in FIXES)):
    fpath = os.path.join(ROOT, fname)
    with open(fpath, "r", encoding="utf-8") as f:
        text = f.read()
    r = text.count("\uff09?")
    print(f"  {fname}: {'CLEAN' if not r else str(r)+' LEFT'}")
