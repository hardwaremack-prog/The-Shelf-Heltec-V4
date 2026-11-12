# microSD wiring diagram for The Shelf on a plain ESP32 dev board (NodeMCU-32S / ESP32-DevKitC, 38 pins)
# CS 5, MOSI 23, CLK 18, MISO 19 (the ESP32's VSPI pins), 3V3 and GND.
# The board is drawn lying down, antenna on the left, USB on the right, chip side up:
# the header with GND · 23 · 22 is along the top, the one with 3V3 · EN along the bottom.
import sys
TOP = ['GND', '23', '22', 'TX', 'RX', '21', 'GND', '19', '18', '5', '17', '16', '4', '0', '2', '15', 'D1', 'D0', 'CLK']
BOT = ['3V3', 'EN', 'VP', 'VN', '34', '35', '32', '33', '25', '26', '27', '14', '12', 'GND', '13', 'D2', 'D3', 'CMD', '5V']

def svg():
    W, H = 1200, 960
    bx, by, bw, bh = 170, 440, 860, 300
    px0 = bx + 70; pstep = (bw - 140) / 18
    pinx = lambda i: px0 + i * pstep
    topy = by + 28; boty = by + bh - 28
    sx, sy, sw, sh = 360, 60, 480, 170
    # (module label, board pin, colour, row, index)
    sd = [('3v3', '3V3', '#ff5a5a', 'bot', 0), ('CS', '5', '#c77dff', 'top', 9), ('MOSI', '23', '#4cd964', 'top', 1),
          ('CLK', '18', '#3aa0ff', 'top', 8), ('MISO', '19', '#ffa53a', 'top', 7), ('GND', 'GND', '#9aa4ae', 'top', 0)]
    sdx = lambda k: sx + 70 + k * 68
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI,Roboto,Helvetica,Arial,sans-serif">',
         f'<rect width="{W}" height="{H}" fill="#0b0f14"/>',
         '<text x="40" y="44" fill="#fff" font-size="28" font-weight="700">microSD card → The Shelf on an ESP32 (NodeMCU / DevKitC)</text>',
         '<text x="40" y="74" fill="#7f93a8" font-size="16">Six jumper wires: five to the header with GND · 23 · 22, and 3V3 from the other side</text>']
    o.append(f'<rect x="{sx}" y="{sy+40}" width="{sw}" height="{sh}" rx="14" fill="#1d4f8f" stroke="#3a78c2" stroke-width="3"/>')
    o.append(f'<rect x="{sx+120}" y="{sy+70}" width="240" height="110" rx="8" fill="#c9ced6" stroke="#8e96a2" stroke-width="3"/>')
    o.append(f'<rect x="{sx+150}" y="{sy+90}" width="180" height="70" rx="4" fill="#2a2f36"/>')
    o.append(f'<text x="{sx+240}" y="{sy+132}" fill="#e6edf5" font-size="18" font-weight="700" text-anchor="middle">microSD</text>')
    o.append(f'<text x="{sx+16}" y="{sy+66}" fill="#a9c7ee" font-size="14">3.3 V SPI module (pin order as printed on it)</text>')
    for k, (lab, _, col, _, _) in enumerate(sd):
        x = sdx(k); y = sy + sh + 40
        o.append(f'<rect x="{x-9}" y="{y-6}" width="18" height="30" rx="{0 if k==0 else 3}" fill="#d4af37" stroke="{"#fff" if k==0 else "#8a6d1d"}" stroke-width="{2 if k==0 else 1}"/>')
        o.append(f'<text x="{x}" y="{y-14}" fill="#fff" font-size="15" font-weight="700" text-anchor="middle">{lab}</text>')
    # the board: antenna end on the left, USB on the right
    o.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="12" fill="#141a22" stroke="#3a4a5c" stroke-width="3"/>')
    o.append(f'<rect x="{bx+bw-12}" y="{by+bh/2-32}" width="46" height="64" rx="8" fill="#9aa4ae" stroke="#5f6b78" stroke-width="3"/>')
    o.append(f'<text x="{bx+bw+11}" y="{by+bh/2+56}" fill="#7f93a8" font-size="14" text-anchor="middle">USB</text>')
    o.append(f'<rect x="{bx+90}" y="{by+70}" width="330" height="160" rx="6" fill="#b9c0c8" stroke="#7d8793" stroke-width="3"/>')
    o.append(f'<path d="M{bx+100},{by+90} h50 v40 h-30 v40 h30 v40 h-50" fill="none" stroke="#d4af37" stroke-width="5"/>')
    o.append(f'<text x="{bx+290}" y="{by+148}" fill="#2a2f36" font-size="20" font-weight="700" text-anchor="middle">ESP32-WROOM-32</text>')
    o.append(f'<text x="{bx+290}" y="{by+176}" fill="#4b5563" font-size="14" text-anchor="middle">antenna end</text>')
    o.append(f'<rect x="{bx+560}" y="{by+120}" width="40" height="40" rx="6" fill="#2b3a4b"/><text x="{bx+580}" y="{by+190}" fill="#7f93a8" font-size="13" text-anchor="middle">BOOT</text>')
    o.append(f'<rect x="{bx+680}" y="{by+120}" width="40" height="40" rx="6" fill="#2b3a4b"/><text x="{bx+700}" y="{by+190}" fill="#7f93a8" font-size="13" text-anchor="middle">EN</text>')
    used = {(c[3], c[4]): c for c in sd}
    for row, names, y, ty in (('top', TOP, topy, topy + 32), ('bot', BOT, boty, boty - 20)):
        for i, name in enumerate(names):
            x = pinx(i); hit = (row, i) in used
            col = used[(row, i)][2] if hit else '#3a4a5c'
            o.append(f'<circle cx="{x}" cy="{y}" r="{11 if hit else 8}" fill="{col if hit else "#0b0f14"}" stroke="{col if hit else "#6b7d90"}" stroke-width="3"/>')
            o.append(f'<text x="{x}" y="{ty}" fill="{"#fff" if hit else "#6b7d90"}" font-size="{14 if hit else 12}" font-weight="{700 if hit else 400}" text-anchor="middle">{name}</text>')
    for k, (lab, pin, col, row, idx) in enumerate(sd):
        x1 = sdx(k); y1 = sy + sh + 64
        if row == 'top':
            x2 = pinx(idx); y2 = topy - 12
            o.append(f'<path d="M{x1},{y1} C{x1},{y1+90} {x2},{y2-110} {x2},{y2}" fill="none" stroke="{col}" stroke-width="6" stroke-linecap="round"/>')
        else:   # 3V3 is on the far header: round the antenna end and come up underneath
            x2 = pinx(idx); y2 = boty + 12; lx = bx - 50
            o.append(f'<path d="M{x1},{y1} C{x1},{y1+70} {lx},{by-60} {lx},{by+40} L{lx},{by+bh+10} C{lx},{by+bh+60} {x2},{by+bh+60} {x2},{y2}" fill="none" stroke="{col}" stroke-width="6" stroke-linecap="round"/>')
    lx, ly = 40, 840
    x = lx
    for a, b, c, _, _ in sd:
        t = f'{a} → {("GPIO "+b) if b.isdigit() else b}'
        o.append(f'<rect x="{x}" y="{ly-14}" width="22" height="8" rx="4" fill="{c}"/>')
        o.append(f'<text x="{x+30}" y="{ly-6}" fill="#d7e3ee" font-size="16" font-weight="600">{t}</text>')
        x += 30 + len(t) * 8.8 + 26
    lines = ['Module pins in silkscreen order: 3v3 · CS · MOSI · CLK · MISO · GND (square pad = 3v3). CLK is the SPI clock (SCK).',
             'Match by the names printed next to the board\'s pins, not by position: ESP32 boards differ (on 30-pin boards 3V3 is next to GND).',
             'The module has no voltage regulator: 3V3 only, never 5V/VIN. Pin 5 may be printed D5 or P5, 23 as D23, and so on.']
    for n, l in enumerate(lines):
        o.append(f'<text x="40" y="{ly+28+n*20}" fill="#7f93a8" font-size="14">{l}</text>')
    o.append('</svg>')
    return '\n'.join(o)

out = sys.argv[1] if len(sys.argv) > 1 else '.'
open(f'{out}/sd-wiring-shelf-esp32.svg', 'w').write(svg())
