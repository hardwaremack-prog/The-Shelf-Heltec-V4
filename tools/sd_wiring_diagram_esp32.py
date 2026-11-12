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


def svg_flipped():
    """The same board turned over (header pins pointing up, USB still on the right): the 3V3 · EN
    row is now along the top and the GND · 23 · 22 row along the bottom, so the card sits below."""
    W, H = 1200, 1060
    bx, by, bw, bh = 170, 180, 860, 300
    px0 = bx + 70; pstep = (bw - 140) / 18
    pinx = lambda i: px0 + i * pstep
    topy = by + 28; boty = by + bh - 28
    sx, sy, sw, sh = 360, 620, 480, 200          # module, pins along its top edge
    sd = [('3v3', '3V3', '#ff5a5a', 'top', 0), ('CS', '5', '#c77dff', 'bot', 9), ('MOSI', '23', '#4cd964', 'bot', 1),
          ('CLK', '18', '#3aa0ff', 'bot', 8), ('MISO', '19', '#ffa53a', 'bot', 7), ('GND', 'GND', '#9aa4ae', 'bot', 0)]
    sdx = lambda k: sx + 70 + k * 68
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI,Roboto,Helvetica,Arial,sans-serif">',
         f'<rect width="{W}" height="{H}" fill="#0b0f14"/>',
         '<text x="40" y="44" fill="#fff" font-size="28" font-weight="700">microSD card → ESP32 NodeMCU, board flipped over</text>',
         '<text x="40" y="74" fill="#7f93a8" font-size="16">Seen from underneath, header pins pointing up, USB on the right. Five wires to the bottom row, 3V3 from the top row.</text>']
    # board, underside: bare PCB, the module's outline dashed (it's on the other side)
    o.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="12" fill="#141a22" stroke="#3a4a5c" stroke-width="3"/>')
    o.append(f'<rect x="{bx+bw-12}" y="{by+bh/2-32}" width="46" height="64" rx="8" fill="#9aa4ae" stroke="#5f6b78" stroke-width="3"/>')
    o.append(f'<text x="{bx+bw+11}" y="{by+bh/2+56}" fill="#7f93a8" font-size="14" text-anchor="middle">USB</text>')
    o.append(f'<rect x="{bx+90}" y="{by+70}" width="330" height="160" rx="6" fill="none" stroke="#4b5b6d" stroke-width="2" stroke-dasharray="8 6"/>')
    o.append(f'<text x="{bx+255}" y="{by+148}" fill="#6b7d90" font-size="18" font-weight="700" text-anchor="middle">underside</text>')
    o.append(f'<text x="{bx+255}" y="{by+174}" fill="#4f6072" font-size="13" text-anchor="middle">(ESP32 module is on the other side; antenna end)</text>')
    o.append(f'<text x="{bx+640}" y="{by+156}" fill="#4f6072" font-size="14" text-anchor="middle">pin names are often printed on this side too</text>')
    used = {(c[3], c[4]): c for c in sd}
    for row, names, y, ty in (('top', BOT, topy, topy + 32), ('bot', TOP, boty, boty - 20)):
        for i, name in enumerate(names):
            x = pinx(i); hit = (row, i) in used
            col = used[(row, i)][2] if hit else '#3a4a5c'
            o.append(f'<circle cx="{x}" cy="{y}" r="{11 if hit else 8}" fill="{col if hit else "#0b0f14"}" stroke="{col if hit else "#6b7d90"}" stroke-width="3"/>')
            o.append(f'<text x="{x}" y="{ty}" fill="{"#fff" if hit else "#6b7d90"}" font-size="{14 if hit else 12}" font-weight="{700 if hit else 400}" text-anchor="middle">{name}</text>')
    # module below the board
    o.append(f'<rect x="{sx}" y="{sy}" width="{sw}" height="{sh}" rx="14" fill="#1d4f8f" stroke="#3a78c2" stroke-width="3"/>')
    o.append(f'<rect x="{sx+120}" y="{sy+40}" width="240" height="110" rx="8" fill="#c9ced6" stroke="#8e96a2" stroke-width="3"/>')
    o.append(f'<rect x="{sx+150}" y="{sy+60}" width="180" height="70" rx="4" fill="#2a2f36"/>')
    o.append(f'<text x="{sx+240}" y="{sy+102}" fill="#e6edf5" font-size="18" font-weight="700" text-anchor="middle">microSD</text>')
    o.append(f'<text x="{sx+16}" y="{sy+sh-14}" fill="#a9c7ee" font-size="14">3.3 V SPI module (pin order as printed on it)</text>')
    for k, (lab, _, col, _, _) in enumerate(sd):
        x = sdx(k); y = sy - 24
        o.append(f'<rect x="{x-9}" y="{y}" width="18" height="30" rx="{0 if k==0 else 3}" fill="#d4af37" stroke="{"#fff" if k==0 else "#8a6d1d"}" stroke-width="{2 if k==0 else 1}"/>')
        o.append(f'<text x="{x}" y="{sy+24}" fill="#fff" font-size="15" font-weight="700" text-anchor="middle">{lab}</text>')
    for k, (lab, pin, col, row, idx) in enumerate(sd):
        x1 = sdx(k); y1 = sy - 24
        x2 = pinx(idx)
        if row == 'bot':
            y2 = boty + 12
            o.append(f'<path d="M{x1},{y1} C{x1},{y1-70} {x2},{y2+80} {x2},{y2}" fill="none" stroke="{col}" stroke-width="6" stroke-linecap="round"/>')
        else:   # 3V3 is on the far row: round the antenna end and come down from above
            y2 = topy - 12; lx = bx - 50
            o.append(f'<path d="M{x1},{y1} C{x1},{y1-60} {lx},{by+bh+60} {lx},{by+bh-40} L{lx},{by-10} C{lx},{by-60} {x2},{by-60} {x2},{y2}" fill="none" stroke="{col}" stroke-width="6" stroke-linecap="round"/>')
    lx, ly = 40, 900
    x = lx
    for a, b, c, _, _ in sd:
        t = f'{a} → {("GPIO "+b) if b.isdigit() else b}'
        o.append(f'<rect x="{x}" y="{ly-14}" width="22" height="8" rx="4" fill="{c}"/>')
        o.append(f'<text x="{x+30}" y="{ly-6}" fill="#d7e3ee" font-size="16" font-weight="600">{t}</text>')
        x += 30 + len(t) * 8.8 + 26
    lines = ['Flipped over, the rows swap places: 3V3 · EN · VP … 5V is now on top, GND · 23 · 22 … CLK along the bottom, still read from the antenna end.',
             'Module pins in silkscreen order: 3v3 · CS · MOSI · CLK · MISO · GND (square pad = 3v3). CLK is the SPI clock (SCK).',
             'Match by the names printed by the pins, not by position: ESP32 boards differ. 3V3 only, never 5V/VIN: the module has no regulator.']
    for n, l in enumerate(lines):
        o.append(f'<text x="40" y="{ly+28+n*20}" fill="#7f93a8" font-size="14">{l}</text>')
    o.append('</svg>')
    return '\n'.join(o)


def svg_inline():
    """In-line: the module plugs straight onto five pins in a row (GND · 19 · 18 · 5 · 17),
    turned around so its GND lands on the board's GND. Its 3v3 pin would land on GPIO 16, so
    that one pin is bent up (or cut short) and one jumper takes it to 3V3."""
    W, H = 1200, 960
    bx, by, bw, bh = 170, 470, 860, 300
    px0 = bx + 70; pstep = (bw - 140) / 18
    pinx = lambda i: px0 + i * pstep
    topy = by + 28; boty = by + bh - 28
    # module pins left to right as it sits (turned around): GND MISO CLK MOSI CS 3v3 over board pins 6..11
    sd = [('GND', 'GND', '#9aa4ae', 6), ('MISO', '19', '#ffa53a', 7), ('CLK', '18', '#3aa0ff', 8),
          ('MOSI', '5', '#4cd964', 9), ('CS', '17', '#c77dff', 10), ('3v3', '3V3', '#ff5a5a', 11)]
    mx0, mx1 = pinx(6) - 70, pinx(11) + 70
    my, mh = 170, 190
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI,Roboto,Helvetica,Arial,sans-serif">',
         f'<rect width="{W}" height="{H}" fill="#0b0f14"/>',
         '<text x="40" y="44" fill="#fff" font-size="28" font-weight="700">microSD in line → The Shelf on an ESP32 (38-pin NodeMCU / DevKitC)</text>',
         '<text x="40" y="74" fill="#7f93a8" font-size="16">The module plugs straight onto GND · 19 · 18 · 5 · 17 (turned around). One jumper: its 3v3 pin to the board\'s 3V3.</text>',
         '<text x="40" y="98" fill="#7f93a8" font-size="16">Bend that one 3v3 pin up (or cut it short) so it doesn\'t go into GPIO 16. Firmware 1.10.1 and later finds the card either way.</text>']
    # the module, upside down over the header (card slot towards the antenna end is fine either way)
    o.append(f'<rect x="{mx0}" y="{my}" width="{mx1-mx0}" height="{mh}" rx="14" fill="#1d4f8f" stroke="#3a78c2" stroke-width="3"/>')
    o.append(f'<rect x="{mx0+90}" y="{my+24}" width="{mx1-mx0-180}" height="96" rx="8" fill="#c9ced6" stroke="#8e96a2" stroke-width="3"/>')
    o.append(f'<rect x="{mx0+120}" y="{my+40}" width="{mx1-mx0-240}" height="62" rx="4" fill="#2a2f36"/>')
    o.append(f'<text x="{(mx0+mx1)/2}" y="{my+78}" fill="#e6edf5" font-size="18" font-weight="700" text-anchor="middle">microSD</text>')
    o.append(f'<text x="{mx0}" y="{my-12}" fill="#a9c7ee" font-size="14">module turned around: its GND end on the left</text>')
    for lab, pin, col, idx in sd:
        x = pinx(idx)
        if lab == '3v3':      # bent up: drawn sticking out sideways, not reaching the header
            o.append(f'<path d="M{x},{my+mh} l0,14 l26,-26" fill="none" stroke="#d4af37" stroke-width="6" stroke-linecap="round"/>')
            o.append(f'<text x="{x+8}" y="{my+mh+44}" fill="#ff8a8a" font-size="13" font-weight="700">bent up</text>')
        else:
            o.append(f'<rect x="{x-5}" y="{my+mh}" width="10" height="{topy-my-mh-10}" fill="#d4af37"/>')
        o.append(f'<text x="{x}" y="{my+mh-30}" fill="#fff" font-size="13" font-weight="700" text-anchor="middle">{lab}</text>')
        o.append(f'<rect x="{x-14}" y="{my+mh-22}" width="28" height="7" rx="3" fill="{col}"/>')
    # board
    o.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="12" fill="#141a22" stroke="#3a4a5c" stroke-width="3"/>')
    o.append(f'<rect x="{bx+bw-12}" y="{by+bh/2-32}" width="46" height="64" rx="8" fill="#9aa4ae" stroke="#5f6b78" stroke-width="3"/>')
    o.append(f'<text x="{bx+bw+11}" y="{by+bh/2+56}" fill="#7f93a8" font-size="14" text-anchor="middle">USB</text>')
    o.append(f'<rect x="{bx+90}" y="{by+70}" width="330" height="160" rx="6" fill="#b9c0c8" stroke="#7d8793" stroke-width="3"/>')
    o.append(f'<path d="M{bx+100},{by+90} h50 v40 h-30 v40 h30 v40 h-50" fill="none" stroke="#d4af37" stroke-width="5"/>')
    o.append(f'<text x="{bx+290}" y="{by+148}" fill="#2a2f36" font-size="20" font-weight="700" text-anchor="middle">ESP32-WROOM-32</text>')
    o.append(f'<text x="{bx+290}" y="{by+176}" fill="#4b5563" font-size="14" text-anchor="middle">antenna end</text>')
    o.append(f'<rect x="{bx+560}" y="{by+120}" width="40" height="40" rx="6" fill="#2b3a4b"/><text x="{bx+580}" y="{by+190}" fill="#7f93a8" font-size="13" text-anchor="middle">BOOT</text>')
    o.append(f'<rect x="{bx+680}" y="{by+120}" width="40" height="40" rx="6" fill="#2b3a4b"/><text x="{bx+700}" y="{by+190}" fill="#7f93a8" font-size="13" text-anchor="middle">EN</text>')
    used = {('top', idx): col for lab, pin, col, idx in sd if lab != '3v3'}
    used[('bot', 0)] = '#ff5a5a'
    for row, names, y, ty in (('top', TOP, topy, topy + 32), ('bot', BOT, boty, boty - 20)):
        for i, name in enumerate(names):
            x = pinx(i); hit = (row, i) in used
            col = used.get((row, i), '#3a4a5c')
            if row == 'top' and i == 11:   # GPIO 16: left empty
                o.append(f'<circle cx="{x}" cy="{y}" r="9" fill="#0b0f14" stroke="#ff8a8a" stroke-width="2" stroke-dasharray="3 3"/>')
                o.append(f'<text x="{x}" y="{ty}" fill="#ff8a8a" font-size="12" text-anchor="middle">16 (empty)</text>')
                continue
            o.append(f'<circle cx="{x}" cy="{y}" r="{11 if hit else 8}" fill="{col if hit else "#0b0f14"}" stroke="{col if hit else "#6b7d90"}" stroke-width="3"/>')
            o.append(f'<text x="{x}" y="{ty}" fill="{"#fff" if hit else "#6b7d90"}" font-size="{14 if hit else 12}" font-weight="{700 if hit else 400}" text-anchor="middle">{name}</text>')
    # the one jumper: bent 3v3 pin round the antenna end to 3V3 on the far header
    # round the USB end and along underneath, clear of the module
    x1, y1 = pinx(11) + 26, my + mh - 12
    x2, y2 = pinx(0), boty + 12; rx = bx + bw + 70; ly2 = by + bh + 45
    o.append(f'<path d="M{x1},{y1} C{x1+120},{y1-30} {rx},{by-80} {rx},{by+20} L{rx},{ly2-30} Q{rx},{ly2} {rx-30},{ly2} L{x2+30},{ly2} Q{x2},{ly2} {x2},{ly2-30} L{x2},{y2}" fill="none" stroke="#ff5a5a" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>')
    o.append(f'<text x="{(x1+rx)/2+30}" y="{by-60}" fill="#ff8a8a" font-size="14" font-weight="700" text-anchor="middle">the one jumper: 3v3 → 3V3</text>')
    lx, ly = 40, 880
    x = lx
    for a, b, c, _ in sd:
        t = f'{a} → {("GPIO "+b) if b.isdigit() else b}'
        o.append(f'<rect x="{x}" y="{ly-14}" width="22" height="8" rx="4" fill="{c}"/>')
        o.append(f'<text x="{x+30}" y="{ly-6}" fill="#d7e3ee" font-size="16" font-weight="600">{t}</text>')
        x += 30 + len(t) * 8.8 + 26
    lines = ['For 38-pin boards whose header reads GND · 23 · 22 · TX · RX · 21 · GND · 19 · 18 · 5 · 17 · 16 … (check the printing first).',
             'The module has no voltage regulator: 3V3 only, never 5V/VIN. GPIO 16 stays empty: never power the card from a GPIO pin.']
    for n, l in enumerate(lines):
        o.append(f'<text x="40" y="{ly+28+n*20}" fill="#7f93a8" font-size="14">{l}</text>')
    o.append('</svg>')
    return '\n'.join(o)


def svg_inline_flipped():
    """In-line, seen from the underside (board turned over, header pins pointing up, USB still on
    the right): the GND · 23 · 22 row is now along the bottom, and the module plugs onto
    GND · 19 · 18 · 5 · 17 there, hanging below the board's edge. One jumper to 3V3 on the top row."""
    W, H = 1200, 1000
    bx, by, bw, bh = 170, 150, 860, 300
    px0 = bx + 70; pstep = (bw - 140) / 18
    pinx = lambda i: px0 + i * pstep
    topy = by + 28; boty = by + bh - 28
    sd = [('GND', 'GND', '#9aa4ae', 6), ('MISO', '19', '#ffa53a', 7), ('CLK', '18', '#3aa0ff', 8),
          ('MOSI', '5', '#4cd964', 9), ('CS', '17', '#c77dff', 10), ('3v3', '3V3', '#ff5a5a', 11)]
    mx0, mx1 = pinx(6) - 70, pinx(11) + 70
    my, mh = by + bh + 70, 190          # module top edge, below the board
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI,Roboto,Helvetica,Arial,sans-serif">',
         f'<rect width="{W}" height="{H}" fill="#0b0f14"/>',
         '<text x="40" y="44" fill="#fff" font-size="28" font-weight="700">microSD in line, on the bottom side of the ESP32</text>',
         '<text x="40" y="74" fill="#7f93a8" font-size="16">Seen from underneath, header pins pointing up, USB on the right. The module plugs onto GND · 19 · 18 · 5 · 17.</text>',
         '<text x="40" y="98" fill="#7f93a8" font-size="16">Bend its 3v3 pin away so it misses GPIO 16, and run one jumper from it to 3V3 on the top row. Firmware 1.10.1 or later.</text>']
    # board, underside
    o.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="12" fill="#141a22" stroke="#3a4a5c" stroke-width="3"/>')
    o.append(f'<rect x="{bx+bw-12}" y="{by+bh/2-32}" width="46" height="64" rx="8" fill="#9aa4ae" stroke="#5f6b78" stroke-width="3"/>')
    o.append(f'<text x="{bx+bw+11}" y="{by+bh/2+56}" fill="#7f93a8" font-size="14" text-anchor="middle">USB</text>')
    o.append(f'<rect x="{bx+90}" y="{by+70}" width="330" height="160" rx="6" fill="none" stroke="#4b5b6d" stroke-width="2" stroke-dasharray="8 6"/>')
    o.append(f'<text x="{bx+255}" y="{by+148}" fill="#6b7d90" font-size="18" font-weight="700" text-anchor="middle">underside</text>')
    o.append(f'<text x="{bx+255}" y="{by+174}" fill="#4f6072" font-size="13" text-anchor="middle">(ESP32 module is on the other side; antenna end)</text>')
    o.append(f'<text x="{bx+640}" y="{by+150}" fill="#4f6072" font-size="14" text-anchor="middle">pin names are often printed on this side too</text>')
    used = {('bot', idx): col for lab, pin, col, idx in sd if lab != '3v3'}
    used[('top', 0)] = '#ff5a5a'
    for row, names, y, ty in (('top', BOT, topy, topy + 32), ('bot', TOP, boty, boty - 20)):
        for i, name in enumerate(names):
            x = pinx(i); hit = (row, i) in used
            col = used.get((row, i), '#3a4a5c')
            if row == 'bot' and i == 11:   # GPIO 16: left empty
                o.append(f'<circle cx="{x}" cy="{y}" r="9" fill="#0b0f14" stroke="#ff8a8a" stroke-width="2" stroke-dasharray="3 3"/>')
                o.append(f'<text x="{x}" y="{ty}" fill="#ff8a8a" font-size="12" text-anchor="middle">16 (empty)</text>')
                continue
            o.append(f'<circle cx="{x}" cy="{y}" r="{11 if hit else 8}" fill="{col if hit else "#0b0f14"}" stroke="{col if hit else "#6b7d90"}" stroke-width="3"/>')
            o.append(f'<text x="{x}" y="{ty}" fill="{"#fff" if hit else "#6b7d90"}" font-size="{14 if hit else 12}" font-weight="{700 if hit else 400}" text-anchor="middle">{name}</text>')
    # the module, pins along its top edge reaching up into the bottom row
    o.append(f'<rect x="{mx0}" y="{my}" width="{mx1-mx0}" height="{mh}" rx="14" fill="#1d4f8f" stroke="#3a78c2" stroke-width="3"/>')
    o.append(f'<rect x="{mx0+90}" y="{my+66}" width="{mx1-mx0-180}" height="96" rx="8" fill="#c9ced6" stroke="#8e96a2" stroke-width="3"/>')
    o.append(f'<rect x="{mx0+120}" y="{my+82}" width="{mx1-mx0-240}" height="62" rx="4" fill="#2a2f36"/>')
    o.append(f'<text x="{(mx0+mx1)/2}" y="{my+120}" fill="#e6edf5" font-size="18" font-weight="700" text-anchor="middle">microSD</text>')
    o.append(f'<text x="{mx0}" y="{my+mh+24}" fill="#a9c7ee" font-size="14">module hangs past the board\'s edge, its GND end towards the antenna</text>')
    for lab, pin, col, idx in sd:
        x = pinx(idx)
        if lab == '3v3':
            o.append(f'<path d="M{x},{my} l0,-14 l26,26" fill="none" stroke="#d4af37" stroke-width="6" stroke-linecap="round"/>')
            o.append(f'<text x="{x+8}" y="{my-40}" fill="#ff8a8a" font-size="13" font-weight="700">bent away</text>')
        else:
            o.append(f'<rect x="{x-5}" y="{boty+10}" width="10" height="{my-boty-10}" fill="#d4af37"/>')
        o.append(f'<rect x="{x-14}" y="{my+16}" width="28" height="7" rx="3" fill="{col}"/>')
        o.append(f'<text x="{x}" y="{my+44}" fill="#fff" font-size="13" font-weight="700" text-anchor="middle">{lab}</text>')
    # the one jumper: from the bent pin, round the USB end, over the top to 3V3 (top row, antenna end)
    x1, y1 = pinx(11) + 26, my + 12
    x2, y2 = pinx(0), topy - 12; rx = bx + bw + 70; ty2 = by - 30
    o.append(f'<path d="M{x1},{y1} C{x1+120},{y1+20} {rx},{by+bh+60} {rx},{by+bh-20} L{rx},{ty2+30} Q{rx},{ty2} {rx-30},{ty2} L{x2+30},{ty2} Q{x2},{ty2} {x2},{ty2+30} L{x2},{y2}" fill="none" stroke="#ff5a5a" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>')
    o.append(f'<text x="{(x1+rx)/2+40}" y="{my+60}" fill="#ff8a8a" font-size="14" font-weight="700" text-anchor="middle">the one jumper: 3v3 → 3V3</text>')
    lx, ly = 40, 900
    x = lx
    for a, b, c, _ in sd:
        t = f'{a} → {("GPIO "+b) if b.isdigit() else b}'
        o.append(f'<rect x="{x}" y="{ly-14}" width="22" height="8" rx="4" fill="{c}"/>')
        o.append(f'<text x="{x+30}" y="{ly-6}" fill="#d7e3ee" font-size="16" font-weight="600">{t}</text>')
        x += 30 + len(t) * 8.8 + 26
    lines = ['Turned over, the rows swap places: 3V3 · EN · VP … 5V on top, GND · 23 · 22 … CLK along the bottom, still read from the antenna end.',
             'Check the printed pin names first: boards differ. 3V3 only, never 5V/VIN. GPIO 16 stays empty: never power the card from a GPIO pin.']
    for n, l in enumerate(lines):
        o.append(f'<text x="40" y="{ly+28+n*20}" fill="#7f93a8" font-size="14">{l}</text>')
    o.append('</svg>')
    return '\n'.join(o)

out = sys.argv[1] if len(sys.argv) > 1 else '.'
open(f'{out}/sd-wiring-shelf-esp32.svg', 'w').write(svg())
open(f'{out}/sd-wiring-shelf-esp32-flipped.svg', 'w').write(svg_flipped())
open(f'{out}/sd-wiring-shelf-esp32-inline.svg', 'w').write(svg_inline())
open(f'{out}/sd-wiring-shelf-esp32-inline-flipped.svg', 'w').write(svg_inline_flipped())
