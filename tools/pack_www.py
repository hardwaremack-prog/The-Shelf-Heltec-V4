#!/usr/bin/env python3
"""Pack The Shelf's web app (its public/ folder) into a LittleFS image for the board.

    python3 tools/pack_www.py <path-to-the-shelf> <out.bin> [size]

- Uses The Shelf's own index.html, app.js, style.css, pdf.js and Tidy Suite unchanged,
  except: the AI and Big Transfer tabs are taken out (they need a computer), and
  board-ui/board.css + board.js are added to the page.
- Everything is stored gzipped; the board serves it with Content-Encoding: gzip.
"""
import gzip, os, re, shutil, subprocess, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
shelf = pathlib.Path(sys.argv[1]).resolve()
out_bin = pathlib.Path(sys.argv[2]).resolve()
size = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x200000
public = shelf / "public"
if not (public / "app.js").exists():
    sys.exit(f"Can't find The Shelf's public/app.js under {shelf}")

stage = ROOT / "build" / "www"
shutil.rmtree(stage, ignore_errors=True)
SKIP_DIRS = {"vendor/novnc"}
SKIP_FILES = {"vnc.html"}
for src in public.rglob("*"):
    rel = src.relative_to(public).as_posix()
    if src.is_dir() or any(rel == d or rel.startswith(d + "/") for d in SKIP_DIRS) or rel in SKIP_FILES:
        continue
    dst = stage / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)

# ---- small, targeted changes to The Shelf's page
app = (stage / "app.js").read_text(encoding="utf-8")
before = app
for key in ("ai", "bigfiles"):
    app = re.sub(r"\n\s*\{ key: '%s',[^\n]*\},?" % key, "", app, count=1)
if app == before:
    sys.exit("Couldn't find the TOOLS list in app.js -- The Shelf's layout changed; update pack_www.py")
app = re.sub(r",(\s*\];\s*\nconst TOOL_LABELS)", r"\1", app)   # no trailing comma left behind
# add the board's own tab: 900MHz Coms (drawn by board.js)
app, n1 = re.subn(r"(\n(\s*)\{ key: 'tidysuite',)", r"\n\2{ key: 'coms', label: '900MHz Coms', color: '#E2725B' },\1", app, count=1)
app, n2 = re.subn(r"(else if \(key === 'tidysuite'\) loadTidySuiteView\(\);)", r"\1\n  else if (key === 'coms') window.loadComsView();", app, count=1)
if not (n1 and n2):
    sys.exit("Couldn't add the 900MHz Coms tab to app.js -- The Shelf's layout changed; update pack_www.py")
(stage / "app.js").write_text(app, encoding="utf-8")

html = (stage / "index.html").read_text(encoding="utf-8")
html = html.replace('<link rel="stylesheet" href="style.css">',
                    '<link rel="stylesheet" href="style.css">\n<link rel="stylesheet" href="board.css">', 1)
html = html.replace('<script src="app.js"></script>', '<script src="app.js"></script>\n<script src="board.js"></script>', 1)
html = html.replace("<title>The Shelf</title>", "<title>The Shelf · Heltec V4</title>", 1)
if "board.js" not in html or "board.css" not in html:
    sys.exit("Couldn't add board.js/board.css to index.html")
(stage / "index.html").write_text(html, encoding="utf-8")

# ---- the board's own bits
for src in (ROOT / "board-ui").rglob("*"):
    if src.is_file():
        dst = stage / src.relative_to(ROOT / "board-ui")
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

try:
    rev = subprocess.run(["git", "-C", str(shelf), "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
except Exception:
    rev = ""
(stage / "shelf-version.txt").write_text((rev or "unknown") + "\n")

# ---- gzip everything
total = 0
for f in sorted(p for p in stage.rglob("*") if p.is_file()):
    data = f.read_bytes()
    gz = gzip.compress(data, 9, mtime=0)
    f.with_name(f.name + ".gz").write_bytes(gz)
    f.unlink()
    total += len(gz)
print(f"web app: {total/1024:.0f} KB gzipped (The Shelf {rev or '?'})")
if total > size * 0.9:
    sys.exit(f"Too big for the {size//1024} KB web partition")

# Needs mklittlefs 3.x built with LFS_NAME_MAX=64 to match the board (the copy bundled with
# the esp32 2.0.x core is too old for paths over 32 characters). See README.
mk = os.environ.get("MKLITTLEFS") or shutil.which("mklittlefs") or "mklittlefs"
subprocess.run([mk, "-c", str(stage), "-b", "4096", "-p", "256", "-s", str(size), str(out_bin)], check=True, stdout=subprocess.DEVNULL)
print(f"wrote {out_bin}")
