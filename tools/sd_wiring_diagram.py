# microSD wiring diagrams for The Shelf on Heltec V4 and V3 (inline layout: CS 42, MOSI 41, CLK 40, MISO 39)
import sys
J3=['GND','3V3','3V3','37','46','45','42','41','40','39','38','1','2','3','4','5','6','7']
J2=['GND','5V','Ve','Ve','44','43','RST','0','36','35','34','33','47','48','26','21','20','19']
def svg(board):
    W,H=1200,900
    bx,by,bw,bh=170,430,860,330    # board
    px0=bx+70; pstep=(bw-140)/17
    pinx=lambda i: px0+i*pstep
    topy=by+28; boty=by+bh-28
    # SD module
    sx,sy,sw,sh=360,60,480,170
    sd=[('3v3','3V3','#ff5a5a',1),('CS','39','#c77dff',9),('MOSI','41','#4cd964',7),('CLK','40','#3aa0ff',8),('MISO','42','#ffa53a',6),('GND','GND','#9aa4ae',0)]
    if True:   # inline layout: CS 42, MISO 39 (module plugs straight into 42-41-40-39)
        sd=[('3v3','3V3','#ff5a5a',1),('CS','42','#c77dff',6),('MOSI','41','#4cd964',7),('CLK','40','#3aa0ff',8),('MISO','39','#ffa53a',9),('GND','GND','#9aa4ae',0)]
    sdx=lambda k: sx+70+k*68
    o=[]
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI,Roboto,Helvetica,Arial,sans-serif">')
    o.append(f'<rect width="{W}" height="{H}" fill="#0b0f14"/>')
    o.append(f'<text x="40" y="44" fill="#fff" font-size="28" font-weight="700">microSD card → The Shelf on Heltec {board}</text>')
    o.append(f'<text x="40" y="74" fill="#7f93a8" font-size="16">Six wires to the J3 header row (the one that starts GND · 3V3 · 3V3)</text>')
    # SD module drawing
    o.append(f'<rect x="{sx}" y="{sy+40}" width="{sw}" height="{sh}" rx="14" fill="#1d4f8f" stroke="#3a78c2" stroke-width="3"/>')
    o.append(f'<rect x="{sx+120}" y="{sy+70}" width="240" height="110" rx="8" fill="#c9ced6" stroke="#8e96a2" stroke-width="3"/>')
    o.append(f'<rect x="{sx+150}" y="{sy+90}" width="180" height="70" rx="4" fill="#2a2f36"/>')
    o.append(f'<text x="{sx+240}" y="{sy+132}" fill="#e6edf5" font-size="18" font-weight="700" text-anchor="middle">microSD</text>')
    o.append(f'<text x="{sx+16}" y="{sy+66}" fill="#a9c7ee" font-size="14">3.3 V SPI module (pin order as printed on it)</text>')
    # SD pins along bottom edge of module
    for k,(lab,_,col,_) in enumerate(sd):
        x=sdx(k); y=sy+sh+40
        o.append(f'<rect x="{x-9}" y="{y-6}" width="18" height="30" rx="{0 if k==0 else 3}" fill="#d4af37" stroke="{"#fff" if k==0 else "#8a6d1d"}" stroke-width="{2 if k==0 else 1}"/>')
        o.append(f'<text x="{x}" y="{y-14}" fill="#fff" font-size="15" font-weight="700" text-anchor="middle">{lab}</text>')
    # board
    o.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="18" fill="#141a22" stroke="#3a4a5c" stroke-width="3"/>')
    o.append(f'<rect x="{bx-34}" y="{by+bh/2-38}" width="46" height="76" rx="10" fill="#9aa4ae" stroke="#5f6b78" stroke-width="3"/>')
    o.append(f'<text x="{bx-11}" y="{by+bh/2+60}" fill="#7f93a8" font-size="14" text-anchor="middle">USB-C</text>')
    o.append(f'<rect x="{bx+bw-60}" y="{by+bh/2-14}" width="28" height="28" rx="4" fill="#d4af37"/>')
    o.append(f'<text x="{bx+bw-46}" y="{by+bh/2+44}" fill="#7f93a8" font-size="14" text-anchor="middle">LoRa ant.</text>')
    o.append(f'<rect x="{bx+250}" y="{by+95}" width="300" height="150" rx="6" fill="#05070a" stroke="#2b3a4b" stroke-width="3"/>')
    o.append(f'<text x="{bx+400}" y="{by+165}" fill="#7fe3ff" font-size="20" font-weight="700" text-anchor="middle">OLED</text>')
    o.append(f'<text x="{bx+400}" y="{by+192}" fill="#7f93a8" font-size="15" text-anchor="middle">Heltec WiFi LoRa 32 {board}</text>')
    o.append(f'<text x="{bx+30}" y="{topy+46}" fill="#7f93a8" font-size="13">J3</text>')
    o.append(f'<text x="{bx+30}" y="{boty-34}" fill="#7f93a8" font-size="13">J2</text>')
    used={c[3]:c for c in sd}
    for i,name in enumerate(J3):
        x=pinx(i); hit=i in used
        col=used[i][2] if hit else '#3a4a5c'
        o.append(f'<circle cx="{x}" cy="{topy}" r="{11 if hit else 8}" fill="{col if hit else "#0b0f14"}" stroke="{col if hit else "#6b7d90"}" stroke-width="3"/>')
        o.append(f'<text x="{x}" y="{topy+32}" fill="{"#fff" if hit else "#6b7d90"}" font-size="{14 if hit else 12}" font-weight="{700 if hit else 400}" text-anchor="middle">{name}</text>')
    for i,name in enumerate(J2):
        x=pinx(i)
        o.append(f'<circle cx="{x}" cy="{boty}" r="8" fill="#0b0f14" stroke="#4b5b6d" stroke-width="3"/>')
        o.append(f'<text x="{x}" y="{boty-18}" fill="#4f6072" font-size="12" text-anchor="middle">{name}</text>')
    # wires
    for k,(lab,pin,col,idx) in enumerate(sd):
        x1=sdx(k); y1=sy+sh+64; x2=pinx(idx); y2=topy-12
        o.append(f'<path d="M{x1},{y1} C{x1},{y1+90} {x2},{y2-110} {x2},{y2}" fill="none" stroke="{col}" stroke-width="6" stroke-linecap="round"/>')
    # legend
    lx,ly=40,790
    items=[(f'{a} → {("GPIO "+b) if b.isdigit() else b}',c) for a,b,c,_ in sd]
    x=lx
    for t,c in items:
        o.append(f'<rect x="{x}" y="{ly-14}" width="22" height="8" rx="4" fill="{c}"/>')
        o.append(f'<text x="{x+30}" y="{ly-6}" fill="#d7e3ee" font-size="16" font-weight="600">{t}</text>')
        x+=30+len(t)*8.8+26
    note='Pin names are printed next to each header pin on the board. Match by name. SD modules order their pins differently, so go by the labels.'
    if board=='V4': note+=' On the V4, GPIO 39–42 are shared with the GNSS connector, so leave the GNSS kit unplugged.'
    lines=['Module pins in silkscreen order: 3v3 · CS · MOSI · CLK · MISO · GND (square pad = 3v3). CLK is the SPI clock (SCK).','Header pin names are printed on the Heltec board. Match by name, not position.']
    lines.append('The module can also plug straight into 42 · 41 · 40 · 39: see the inline mounting diagram.' + (' Leave the V4 GNSS kit unplugged (it uses GPIO 39–42).' if board=='V4' else ''))
    lines.append('This module has no voltage regulator: 3V3 only, never 5V. The V4-R8 uses different pins: see its own diagram.' if board=='V4' else 'This module has no voltage regulator: 3V3 only, never 5V.')
    for n,l in enumerate(lines): o.append(f'<text x="40" y="{ly+28+n*20}" fill="#7f93a8" font-size="14">{l}</text>')
    o.append('</svg>')
    return '\n'.join(o)
import sys
out=sys.argv[1] if len(sys.argv)>1 else '.'
for b in ['V3','V4']:
    open(f'{out}/sd-wiring-shelf-{b.lower()}.svg','w').write(svg(b))
