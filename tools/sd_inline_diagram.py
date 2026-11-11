# Inline microSD mounting diagram for The Shelf on Heltec V4 / V3 (CS 42, MOSI 41, CLK 40, MISO 39)
J3 = ['GND', '3V3', '3V3', '37', '46', '45', '42', '41', '40', '39', '38', '1', '2', '3', '4', '5', '6', '7']
W, H = 1200, 1160
bx, by, bw, bh = 170, 560, 860, 300
px0 = bx + 70; step = (bw - 140) / 17
px = lambda i: px0 + i * step
topy = by + 28
# module sits right over J3 positions 5..10 (45, 42, 41, 40, 39, 38)
MOD = [('3v3', 5, '#ff5a5a'), ('CS', 6, '#c77dff'), ('MOSI', 7, '#4cd964'), ('CLK', 8, '#3aa0ff'), ('MISO', 9, '#ffa53a'), ('GND', 10, '#9aa4ae')]
mx0, mx1 = px(5) - 40, px(10) + 40
my = 330            # module bottom edge (pin row)
o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI,Roboto,Helvetica,Arial,sans-serif">',
     f'<rect width="{W}" height="{H}" fill="#0b0f14"/>',
     '<text x="40" y="44" fill="#fff" font-size="28" font-weight="700">Inline microSD mounting → The Shelf on Heltec V4 / V3</text>',
     '<text x="40" y="74" fill="#7f93a8" font-size="16">4 signal pins plug straight into J3 (42 · 41 · 40 · 39). Only power needs 2 short jumpers. The Shelf v1.6.0+ (not the V4-R8)</text>']
# module body
o.append(f'<rect x="{mx0}" y="{my-120}" width="{mx1-mx0}" height="120" rx="12" fill="#1d4f8f" stroke="#3a78c2" stroke-width="3"/>')
o.append(f'<rect x="{mx0+60}" y="{my-104}" width="{mx1-mx0-120}" height="62" rx="6" fill="#c9ced6" stroke="#8e96a2" stroke-width="2"/>')
o.append(f'<text x="{(mx0+mx1)/2}" y="{my-66}" fill="#2a2f36" font-size="16" font-weight="700" text-anchor="middle">microSD module</text>')
for lab, i, col in MOD:
    x = px(i)
    o.append(f'<text x="{x}" y="{my-12}" fill="#fff" font-size="14" font-weight="700" text-anchor="middle">{lab}</text>')
# board
o.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="18" fill="#141a22" stroke="#3a4a5c" stroke-width="3"/>')
o.append(f'<rect x="{bx-34}" y="{by+bh/2-38}" width="46" height="76" rx="10" fill="#9aa4ae" stroke="#5f6b78" stroke-width="3"/>')
o.append(f'<text x="{bx-11}" y="{by+bh/2+60}" fill="#7f93a8" font-size="14" text-anchor="middle">USB-C</text>')
o.append(f'<rect x="{bx+250}" y="{by+120}" width="300" height="130" rx="6" fill="#05070a" stroke="#2b3a4b" stroke-width="3"/>')
o.append(f'<text x="{bx+400}" y="{by+180}" fill="#7fe3ff" font-size="20" font-weight="700" text-anchor="middle">OLED</text>')
o.append(f'<text x="{bx+400}" y="{by+207}" fill="#7f93a8" font-size="15" text-anchor="middle">Heltec WiFi LoRa 32 V4 / V3</text>')
o.append(f'<text x="{bx+30}" y="{topy+46}" fill="#7f93a8" font-size="13">J3</text>')
hot = {i: c for _, i, c in MOD if i in (6, 7, 8, 9)}
for i, n in enumerate(J3):
    x = px(i); c = hot.get(i)
    pw = i in (0, 2)
    col = c or ('#9aa4ae' if i == 0 else '#ff5a5a' if i == 2 else None)
    o.append(f'<circle cx="{x}" cy="{topy}" r="{11 if col else 8}" fill="{col or "#0b0f14"}" stroke="{col or "#6b7d90"}" stroke-width="3"/>')
    o.append(f'<text x="{x}" y="{topy+32}" fill="{"#fff" if col else "#6b7d90"}" font-size="{14 if col else 12}" font-weight="{700 if col else 400}" text-anchor="middle">{n}</text>')
# straight pins for the 4 signals
for lab, i, col in MOD:
    x = px(i)
    if i in (6, 7, 8, 9):
        o.append(f'<rect x="{x-4}" y="{my}" width="8" height="{topy-my-12}" fill="#d4af37"/>')
        o.append(f'<rect x="{x-12}" y="{topy-34}" width="24" height="18" rx="3" fill="#222a33" stroke="#4b5b6d"/>')   # black plastic of header
        o.append(f'<line x1="{x}" y1="{my+40}" x2="{x}" y2="{topy-40}" stroke="{col}" stroke-width="4" stroke-dasharray="2 8" stroke-linecap="round"/>')
# bent-up power pins (not in the header) + their jumpers
for lab, i, col in MOD:
    if i not in (5, 10): continue
    x = px(i); tip = (x - 34, my + 22) if i == 5 else (x + 34, my + 22)
    o.append(f'<path d="M{x},{my} L{x},{my+14} L{tip[0]},{tip[1]}" fill="none" stroke="#d4af37" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/>')
    o.append(f'<circle cx="{x}" cy="{topy}" r="15" fill="none" stroke="#ff6b6b" stroke-width="3"/>')
    o.append(f'<line x1="{x-10}" y1="{topy-10}" x2="{x+10}" y2="{topy+10}" stroke="#ff6b6b" stroke-width="3"/><line x1="{x+10}" y1="{topy-10}" x2="{x-10}" y2="{topy+10}" stroke="#ff6b6b" stroke-width="3"/>')
    if i == 5:
        dest = px(2)
        o.append(f'<path d="M{tip[0]},{tip[1]} C{tip[0]},{my+150} {dest},{my+120} {dest},{topy-12}" fill="none" stroke="{col}" stroke-width="6" stroke-linecap="round"/>')
    else:   # GND: up and over the top of the module, then down to the first pin of the row
        top = my - 160; left = px(0)
        o.append(f'<path d="M{tip[0]},{tip[1]} C{tip[0]+60},{tip[1]} {tip[0]+60},{top} {tip[0]},{top} L{left+60},{top} C{left},{top} {left},{top+60} {left},{top+120} L{left},{topy-12}" fill="none" stroke="{col}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>')
o.append(f'<text x="{px(5)}" y="{topy+58}" fill="#ff8f8f" font-size="13" text-anchor="middle">45: leave empty</text>')
o.append(f'<text x="{px(10)}" y="{topy+58}" fill="#ff8f8f" font-size="13" text-anchor="middle">38: leave empty</text>')
o.append(f'<text x="{px(1)-20}" y="{my+70}" fill="#ff8f8f" font-size="14" font-weight="600" text-anchor="middle">3v3 jumper</text>')
o.append(f'<text x="{px(2)}" y="{my-170}" fill="#c3cad2" font-size="14" font-weight="600" text-anchor="middle">GND jumper (over the module)</text>')
# side view inset
sx, sy = 40, by + bh + 46
o.append(f'<text x="{sx}" y="{sy}" fill="#fff" font-size="17" font-weight="700">Side view: 2 ways to fit it</text>')
def side(x0, title, mode):
    y0 = sy + 40
    o.append(f'<text x="{x0}" y="{y0}" fill="#7fe3ff" font-size="14" font-weight="600">{title}</text>')
    o.append(f'<rect x="{x0}" y="{y0+110}" width="300" height="10" fill="#1f6b3a"/>')        # heltec pcb edge-on
    o.append(f'<text x="{x0+150}" y="{y0+138}" fill="#7f93a8" font-size="12" text-anchor="middle">Heltec board</text>')
    o.append(f'<rect x="{x0+40}" y="{y0+86}" width="20" height="24" fill="#222a33"/>')        # female header on Heltec
    if mode == 'up':
        o.append(f'<rect x="{x0+47}" y="{y0+30}" width="6" height="60" fill="#d4af37"/>')
        o.append(f'<rect x="{x0+36}" y="{y0+10}" width="8" height="80" fill="#1d4f8f"/>')
        o.append(f'<text x="{x0+80}" y="{y0+50}" fill="#c3cad2" font-size="12">module stands up</text>')
        o.append(f'<text x="{x0+80}" y="{y0+66}" fill="#c3cad2" font-size="12">(straight pins)</text>')
    else:
        o.append(f'<path d="M{x0+50},{y0+90} L{x0+50},{y0+70} L{x0+90},{y0+70}" fill="none" stroke="#d4af37" stroke-width="6"/>')
        o.append(f'<rect x="{x0+90}" y="{y0+62}" width="150" height="8" fill="#1d4f8f"/>')
        o.append(f'<text x="{x0+90}" y="{y0+52}" fill="#c3cad2" font-size="12">module lies flat over the board</text>')
        o.append(f'<text x="{x0+90}" y="{y0+90}" fill="#c3cad2" font-size="12">(right-angle pins)</text>')
side(60, 'A · straight header', 'up')
side(420, 'B · right-angle header (lower profile)', 'flat')
# legend
lx = 780
rows = [('3v3', 'jumper → 3V3 (3rd pin of the row)', '#ff5a5a'), ('CS', 'straight into GPIO 42', '#c77dff'), ('MOSI', 'straight into GPIO 41', '#4cd964'),
        ('CLK', 'straight into GPIO 40', '#3aa0ff'), ('MISO', 'straight into GPIO 39', '#ffa53a'), ('GND', 'jumper → GND (1st pin of the row)', '#9aa4ae')]
o.append(f'<text x="{lx}" y="{sy}" fill="#fff" font-size="17" font-weight="700">Connections</text>')
for k, (a, b, c) in enumerate(rows):
    y = sy + 34 + k * 26
    o.append(f'<rect x="{lx}" y="{y-10}" width="22" height="8" rx="4" fill="{c}"/>')
    o.append(f'<text x="{lx+30}" y="{y}" fill="#d7e3ee" font-size="15"><tspan font-weight="700">{a}</tspan> {b}</text>')
notes = ['Bend the module\'s 3v3 and GND pins up (or clip them) so they don\'t touch GPIO 45 and 38. Then add the two jumpers.',
         'Go by the pin names printed on the Heltec board. On a V4, leave the GNSS kit unplugged, because it also uses GPIO 39–42.']
for n, t in enumerate(notes):
    o.append(f'<text x="40" y="{H-48+n*20}" fill="#7f93a8" font-size="14">{t}</text>')
o.append('</svg>')
import sys
open((sys.argv[1] if len(sys.argv) > 1 else '.') + '/sd-inline-shelf.svg', 'w').write('\n'.join(o))
