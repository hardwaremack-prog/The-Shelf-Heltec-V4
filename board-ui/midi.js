// The Shelf for Heltec: MIDI playback in the Music shelf.
// The MIDI parser and General MIDI synth are from MIDI Vault (Heltec-MIDI), unchanged.
(function () {
"use strict";
const store = { get: (k, d) => d, set() {} };
const stripExt = n => n.replace(/\.(midi?|kar|rmi|smf)$/i, "");
const GM = ["Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano","Electric Piano 1","Electric Piano 2","Harpsichord","Clavinet",
"Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer",
"Drawbar Organ","Percussive Organ","Rock Organ","Church Organ","Reed Organ","Accordion","Harmonica","Tango Accordion",
"Nylon Guitar","Steel Guitar","Jazz Guitar","Clean Electric Guitar","Muted Guitar","Overdriven Guitar","Distortion Guitar","Guitar Harmonics",
"Acoustic Bass","Fingered Bass","Picked Bass","Fretless Bass","Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
"Violin","Viola","Cello","Contrabass","Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
"String Ensemble 1","String Ensemble 2","Synth Strings 1","Synth Strings 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
"Trumpet","Trombone","Tuba","Muted Trumpet","French Horn","Brass Section","Synth Brass 1","Synth Brass 2",
"Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax","Oboe","English Horn","Bassoon","Clarinet",
"Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina",
"Square Lead","Sawtooth Lead","Calliope Lead","Chiff Lead","Charang Lead","Voice Lead","Fifths Lead","Bass + Lead",
"New Age Pad","Warm Pad","Polysynth Pad","Choir Pad","Bowed Pad","Metallic Pad","Halo Pad","Sweep Pad",
"Rain FX","Soundtrack FX","Crystal FX","Atmosphere FX","Brightness FX","Goblins FX","Echoes FX","Sci-fi FX",
"Sitar","Banjo","Shamisen","Koto","Kalimba","Bagpipe","Fiddle","Shanai",
"Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
"Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet","Telephone Ring","Helicopter","Applause","Gunshot"];
const CH_COLORS = ["#38e1ff","#b388ff","#ff5fa2","#ffd166","#5df2a0","#ff8a3d","#4d9fff","#f15bb5","#9bf6ff","#c6ff5c","#ff6b6b","#7c83ff","#00f5d4","#fee440","#e0aaff","#80ffdb"];
const DRUM_COLOR = "#c9cfdf";

/* ===================================================== MIDI parser */
function decodeText(bytes) {
  try { return new TextDecoder("utf-8", { fatal: true }).decode(bytes); }
  catch (e) { try { return new TextDecoder("windows-1252").decode(bytes); } catch (e2) { return String.fromCharCode(...bytes); } }
}
function parseMidi(buf, fileName) {
  const d = new Uint8Array(buf);
  let off = -1;
  for (let i = 0; i + 14 <= d.length; i++) if (d[i] === 0x4d && d[i+1] === 0x54 && d[i+2] === 0x68 && d[i+3] === 0x64) { off = i; break; }
  if (off < 0) throw new Error("This doesn't look like a MIDI file");
  const be32 = i => ((d[i] << 24) | (d[i+1] << 16) | (d[i+2] << 8) | d[i+3]) >>> 0, be16 = i => (d[i] << 8) | d[i+1];
  const hl = be32(off + 4), format = be16(off + 8), ntr = be16(off + 10), div = be16(off + 12);
  let smpte = false, ppq = div || 480, tps = 0;
  if (div & 0x8000) { smpte = true; tps = (256 - (div >> 8)) * (div & 0xff); }
  let p = off + 8 + hl;
  const evs = []; const trackNames = []; let ord = 0, t = 0;
  while (t < ntr && p + 8 <= d.length) {
    const id = String.fromCharCode(d[p], d[p+1], d[p+2], d[p+3]); const len = be32(p + 4); p += 8;
    const end = Math.min(p + len, d.length);
    if (id !== "MTrk") { p = end; continue; }
    let tick = 0, rs = 0, q = p;
    const vlq = () => { let v = 0; for (let k = 0; k < 4 && q < end; k++) { const b = d[q++]; v = (v << 7) | (b & 0x7f); if (!(b & 0x80)) break; } return v; };
    while (q < end) {
      tick += vlq(); if (q >= end) break;
      let st = d[q];
      if (st & 0x80) q++; else { if (!rs) { q++; continue; } st = rs; }
      if (st === 0xff) {
        const type = d[q++], l = vlq(); const data = d.subarray(q, Math.min(q + l, end)); q += l;
        if (type === 0x2f) break;
        if (type === 0x03 && trackNames[t] === undefined) trackNames[t] = decodeText(data).trim();
        evs.push({ tick, ord: ord++, trk: t, meta: type, data });
      } else if (st === 0xf0 || st === 0xf7) { const l = vlq(); q += l; }
      else {
        rs = st; const hi = st & 0xf0;
        const a = d[q++] & 0x7f; let b = 0; if (hi !== 0xc0 && hi !== 0xd0) b = d[q++] & 0x7f;
        evs.push({ tick, ord: ord++, trk: t, st: hi, ch: st & 15, a, b });
      }
    }
    p = end; t++;
  }
  evs.sort((x, y) => x.tick - y.tick || x.ord - y.ord);
  // ticks -> seconds
  let tempo = 500000, lastTick = 0, sec = 0; const tempos = [];
  for (const e of evs) {
    sec += smpte ? (e.tick - lastTick) / tps : (e.tick - lastTick) * tempo / 1e6 / ppq;
    lastTick = e.tick; e.t = sec;
    if (e.meta === 0x51 && e.data.length >= 3) { tempo = (e.data[0] << 16) | (e.data[1] << 8) | e.data[2]; tempos.push({ t: sec, bpm: 6e7 / tempo }); }
  }
  // notes, channel info, texts
  const notes = [], open = new Map(), firstProg = new Array(16).fill(-1), used = new Set();
  const texts = []; let title = "";
  const playable = [];
  for (const e of evs) {
    if (e.meta !== undefined) {
      if ([1, 2, 3, 5, 6].includes(e.meta)) texts.push({ t: e.t, type: e.meta, trk: e.trk, s: decodeText(e.data) });
      continue;
    }
    playable.push(e);
    if (e.st === 0xc0 && firstProg[e.ch] < 0) firstProg[e.ch] = e.a;
    const key = e.ch * 128 + e.a;
    if (e.st === 0x90 && e.b > 0) {
      if (!open.has(key)) open.set(key, []);
      open.get(key).push({ start: e.t, vel: e.b, trk: e.trk });
      used.add(e.ch);
    } else if (e.st === 0x80 || (e.st === 0x90 && e.b === 0)) {
      const st = open.get(key); if (st && st.length) { const o = st.shift(); notes.push({ pitch: e.a, ch: e.ch, start: o.start, end: Math.max(e.t, o.start + 0.03), vel: o.vel }); }
    }
  }
  let dur = evs.length ? evs[evs.length - 1].t : 0;
  for (const [key, arr] of open) for (const o of arr) notes.push({ pitch: key & 127, ch: key >> 7, start: o.start, end: Math.max(dur, o.start + 0.5), vel: o.vel });
  for (const n of notes) if (n.end > dur) dur = n.end;
  notes.sort((a, b) => a.start - b.start || a.pitch - b.pitch);
  // title
  const t0 = texts.find(x => x.type === 3 && x.trk === 0 && x.s.trim() && !/^(untitled|track ?\d*|sequence|seq-?\d*|words|lyrics|melody|piano|drums?|bass|tempo|meta|conductor)$/i.test(x.s.trim()));
  title = t0 ? t0.s.trim() : "";
  const karT = texts.find(x => x.s.startsWith("@T") && x.s.length > 2);
  if (karT) title = karT.s.slice(2).trim();
  const fileTitle = stripExt(fileName || "");
  // lyrics
  const isKar = /\.kar$/i.test(fileName || "") || texts.some(x => x.s.startsWith("@KMIDI"));
  let sylls = texts.filter(x => x.type === 5 && x.s.trim().length);
  if (isKar || sylls.length < 4) {
    const k = texts.filter(x => x.type === 1 && !x.s.startsWith("@") && x.t > 0);
    if (isKar && k.length) sylls = k; else if (sylls.length < 4) sylls = [];
  }
  const lines = []; let cur = null;
  for (const s of sylls) {
    let tx = s.s.replace(/\r\n?/g, "\n");
    let brk = /^[\\/\n]/.test(tx) || !cur;
    tx = tx.replace(/^[\\/\n]+/, "");
    if (brk) { cur = { start: s.t, sylls: [] }; lines.push(cur); }
    if (tx.length) cur.sylls.push({ t: s.t, s: tx.replace(/[\\/\n]/g, "") });
    if (/[\n]$/.test(s.s)) cur = null;
  }
  for (let i = 0; i < lines.length; i++) lines[i].end = i + 1 < lines.length ? lines[i + 1].start : lines[i].start + 6;
  const info = texts.filter(x => x.type === 2 || (x.type === 1 && x.t === 0 && !x.s.startsWith("@"))).map(x => x.s.trim()).filter(Boolean);
  return { format, ntr, ppq, duration: dur + 0.6, notes, events: playable, firstProg, used: [...used].sort((a, b) => a - b),
           title: title && title.toLowerCase() !== fileTitle.toLowerCase() ? title : "", fileTitle,
           info, lines, bpm: tempos.length ? tempos[0].bpm : 120, tempoChanges: tempos.length };
}

/* ===================================================== WebAudio GM synth */
class Synth {
  constructor(ctx) {
    this.ctx = ctx;
    this.master = ctx.createGain(); this.master.gain.value = 0.8;
    this.comp = ctx.createDynamicsCompressor();
    this.comp.threshold.value = -14; this.comp.ratio.value = 4; this.comp.attack.value = 0.004; this.comp.release.value = 0.2;
    this.master.connect(this.comp).connect(ctx.destination);
    this.reverb = ctx.createConvolver(); this.reverb.buffer = this.makeIR(2.4);
    this.revOut = ctx.createGain(); this.revOut.gain.value = 0.55;
    this.reverb.connect(this.revOut).connect(this.master);
    this.noise = this.makeNoise();
    this.ch = [];
    for (let c = 0; c < 16; c++) {
      const g = ctx.createGain(), pan = ctx.createStereoPanner ? ctx.createStereoPanner() : ctx.createGain(), send = ctx.createGain();
      g.connect(pan).connect(this.master); g.connect(send).connect(this.reverb);
      this.ch.push({ g, pan, send, prog: 0, vol: 100, expr: 127, sustain: false, bend: 0, rpn: 0x3fff, bendRange: 2, voices: new Set() });
    }
    this.reset();
    this.active = [];
  }
  makeIR(sec) {
    const sr = this.ctx.sampleRate, len = sr * sec | 0, b = this.ctx.createBuffer(2, len, sr);
    for (let c = 0; c < 2; c++) { const d = b.getChannelData(c); for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 3.2) * (i < sr * 0.01 ? i / (sr * 0.01) : 1); }
    return b;
  }
  makeNoise() { const sr = this.ctx.sampleRate, b = this.ctx.createBuffer(1, sr * 2, sr), d = b.getChannelData(0); for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1; return b; }
  reset() {
    for (let c = 0; c < 16; c++) { const ch = this.ch[c]; ch.prog = 0; ch.vol = 100; ch.expr = 127; ch.sustain = false; ch.bend = 0; ch.bendRange = 2; ch.rpn = 0x3fff; this.applyGain(c); this.setPan(c, 64); ch.send.gain.value = 40 / 127 * 0.7; }
  }
  applyGain(c, t) { const ch = this.ch[c], v = Math.pow(ch.vol / 127, 1.6) * Math.pow(ch.expr / 127, 1.6); t = t || this.ctx.currentTime; ch.g.gain.setTargetAtTime(v, t, 0.01); }
  setPan(c, v, t) { const p = this.ch[c].pan; if (p.pan) p.pan.setTargetAtTime((v - 64) / 64, t || this.ctx.currentTime, 0.01); }
  control(c, cc, v, t) {
    const ch = this.ch[c];
    switch (cc) {
      case 7: ch.vol = v; this.applyGain(c, t); break;
      case 11: ch.expr = v; this.applyGain(c, t); break;
      case 10: this.setPan(c, v, t); break;
      case 91: ch.send.gain.setTargetAtTime(v / 127 * 0.7, t || this.ctx.currentTime, 0.02); break;
      case 64: ch.sustain = v >= 64; if (!ch.sustain) for (const vo of [...ch.voices]) if (vo.held) this.release(vo, t); break;
      case 101: ch.rpn = (ch.rpn & 127) | (v << 7); break;
      case 100: ch.rpn = (ch.rpn & 0x3f80) | v; break;
      case 6: if (ch.rpn === 0) ch.bendRange = Math.min(24, v); break;
      case 120: case 123: for (const vo of [...ch.voices]) this.release(vo, t, cc === 120); break;
      case 121: ch.vol = 100; ch.expr = 127; ch.sustain = false; ch.bend = 0; this.applyGain(c, t); break;
    }
  }
  program(c, p) { this.ch[c].prog = p; }
  pitchBend(c, v14, t) {
    const ch = this.ch[c]; ch.bend = (v14 - 8192) / 8192 * ch.bendRange;
    for (const vo of ch.voices) for (const o of vo.oscs) if (o.detune) o.detune.setValueAtTime(ch.bend * 100 + (o._det || 0), t || this.ctx.currentTime);
  }
  allOff(hard) { const t = this.ctx.currentTime; for (const vo of [...this.active]) this.release(vo, t, hard); }
  steal() { if (this.active.length < 110) return; const vo = this.active.find(v => v.released) || this.active[0]; this.release(vo, this.ctx.currentTime, true); }
  noteOn(c, pitch, vel, t) {
    this.steal();
    const ctx = this.ctx, ch = this.ch[c];
    t = Math.max(t, ctx.currentTime);
    const vo = c === 9 ? this.drum(pitch, vel, t, ch) : this.tone(ch.prog, pitch, vel, t, ch);
    if (!vo) return null;
    vo.c = c; vo.pitch = pitch; vo.released = false; vo.held = false;
    vo.out.connect(ch.g);
    ch.voices.add(vo); this.active.push(vo);
    const done = () => { if (vo.dead) return; vo.dead = true; ch.voices.delete(vo); const i = this.active.indexOf(vo); if (i >= 0) this.active.splice(i, 1); try { vo.out.disconnect(); } catch (e) {} };
    vo.oscs[0].onended = done;
    return vo;
  }
  noteOff(vo, t) { if (!vo || vo.released) return; if (this.ch[vo.c].sustain) { vo.held = true; return; } this.release(vo, t); }
  release(vo, t, fast) {
    if (vo.released && !fast) return;
    vo.released = true; vo.held = false;
    t = Math.max(t || 0, this.ctx.currentTime);
    const r = fast ? 0.02 : vo.rel;
    vo.relGain.gain.setValueAtTime(1, t);
    vo.relGain.gain.setTargetAtTime(0, t, r / 4);
    const stopAt = t + r * 1.4 + 0.05;
    for (const o of vo.oscs) { try { o.stop(Math.min(stopAt, o._stop || stopAt)); } catch (e) {} }
  }
  // ---- melodic instruments
  patch(prog) {
    const f = prog >> 3;
    const P = (o) => Object.assign({ w: [["triangle", 1, 1, 0]], a: 0.01, d: 1, s: 0.8, r: 0.25, cut: 0, cutEnv: 0, q: 0.7, vib: 0, lvl: 0.32 }, o);
    switch (f) {
      case 0: if (prog === 4 || prog === 5) return P({ w: [["sine", 1, 1, 0], ["sine", 2, 0.25, 4], ["triangle", 1, 0.2, -4]], a: 0.004, d: 2.2, s: 0, r: 0.4, trem: 1 });
              if (prog === 6) return P({ w: [["sawtooth", 1, 0.7, 0], ["square", 2, 0.2, 0]], a: 0.002, d: 1.4, s: 0, r: 0.12, cut: 5200, lvl: 0.22 });
              if (prog === 7) return P({ w: [["square", 1, 0.6, 0], ["sawtooth", 2, 0.25, 0]], a: 0.002, d: 0.9, s: 0, r: 0.08, cut: 3200, cutEnv: 1, lvl: 0.22 });
              return P({ w: [["triangle", 1, 0.75, 0], ["sine", 2, 0.22, 0], ["sawtooth", 1, 0.07, 3]], a: 0.003, d: 2.6, s: 0, r: 0.35, cut: 4200, cutEnv: 1, piano: 1 });
      case 1: return P({ w: [["sine", 1, 1, 0], ["sine", prog === 14 ? 2.76 : 4, 0.35, 0], ["sine", 3, 0.12, 0]], a: 0.001, d: prog === 11 ? 2.5 : prog >= 12 && prog <= 13 ? 0.5 : 1.8, s: 0, r: 0.4, lvl: 0.34 });
      case 2: if (prog >= 20) return P({ w: [["sawtooth", 1, 0.6, 0], ["sawtooth", 1, 0.4, 9]], a: 0.04, s: 0.85, r: 0.12, cut: 2400, lvl: 0.18, vib: prog === 22 ? 8 : 0 });
              return P({ w: [["sine", 1, 0.7, 0], ["sine", 2, 0.45, 0], ["sine", 4, 0.25, 0], ["sine", 0.5, 0.35, 0], ["sine", 8, 0.08, 0]], a: 0.008, s: 1, r: prog === 19 ? 0.6 : 0.07, lvl: 0.22 });
      case 3: if (prog === 29 || prog === 30) return P({ w: [["sawtooth", 1, 0.6, 0], ["square", 1, 0.5, 7], ["sawtooth", 0.5, 0.3, 0]], a: 0.004, d: 3, s: 0.6, r: 0.18, cut: 2600, q: 2, lvl: 0.16, drive: 1 });
              if (prog === 28) return P({ w: [["sawtooth", 1, 1, 0]], a: 0.002, d: 0.18, s: 0, r: 0.05, cut: 1600, lvl: 0.24 });
              return P({ w: [["sawtooth", 1, 0.6, 0], ["triangle", 1, 0.6, 0]], a: 0.002, d: prog === 24 ? 1.6 : 2.2, s: 0, r: 0.2, cut: 3800, cutEnv: 1, lvl: 0.26 });
      case 4: if (prog >= 38) return P({ w: [["sawtooth", 1, 0.8, 0], ["square", 1, 0.4, -6]], a: 0.003, d: 0.6, s: 0.5, r: 0.1, cut: 1800, cutEnv: 1, q: 4, lvl: 0.26 });
              return P({ w: [["triangle", 1, 1, 0], ["sawtooth", 1, 0.25, 0], ["sine", 0.5, 0.2, 0]], a: 0.004, d: prog === 35 ? 3 : 1.4, s: 0.35, r: 0.12, cut: prog >= 36 ? 2400 : 1200, lvl: 0.42 });
      case 5: if (prog === 45) return P({ w: [["triangle", 1, 1, 0], ["sawtooth", 1, 0.2, 0]], a: 0.002, d: 0.45, s: 0, r: 0.1, cut: 2200, lvl: 0.34 });
              if (prog === 46) return P({ w: [["triangle", 1, 1, 0], ["sine", 2, 0.3, 0]], a: 0.002, d: 2.4, s: 0, r: 0.5, lvl: 0.34 });
              if (prog === 47) return P({ w: [["sine", 1, 1, 0], ["sine", 1.5, 0.3, 0]], a: 0.002, d: 1.6, s: 0, r: 0.4, lvl: 0.5, drop: 1 });
              return P({ w: [["sawtooth", 1, 0.55, -7], ["sawtooth", 1, 0.55, 7], ["sawtooth", 2, 0.12, 0]], a: prog === 44 ? 0.02 : 0.09, s: 0.85, r: 0.4, cut: 3000, q: 0.5, vib: 9, lvl: 0.17, trem: prog === 44 ? 2 : 0 });
      case 6: if (prog >= 52 && prog <= 54) return P({ w: [["triangle", 1, 0.7, -6], ["triangle", 1, 0.7, 6], ["sine", 2, 0.25, 0]], a: 0.25, s: 0.85, r: 0.6, cut: 1800, vib: 7, lvl: 0.24 });
              if (prog === 55) return P({ w: [["sawtooth", 1, 0.6, 0], ["sawtooth", 1.5, 0.4, 0], ["sawtooth", 2, 0.4, 0]], a: 0.003, d: 0.35, s: 0, r: 0.2, cut: 3000, lvl: 0.22 });
              return P({ w: [["sawtooth", 1, 0.5, -11], ["sawtooth", 1, 0.5, 11], ["sawtooth", 1, 0.4, 0]], a: 0.16, s: 0.85, r: 0.6, cut: prog >= 50 ? 3500 : 2400, vib: 6, lvl: 0.15 });
      case 7: return P({ w: [["sawtooth", 1, 0.8, 0], ["sawtooth", 1, 0.4, 6]], a: prog === 60 ? 0.07 : 0.04, s: 0.8, r: 0.15, cut: prog === 59 ? 1200 : 2600, cutEnv: 2, q: 1, vib: 6, lvl: 0.2 });
      case 8: return P({ w: prog <= 67 ? [["sawtooth", 1, 0.6, 0], ["square", 1, 0.4, 0]] : [["square", 1, 0.8, 0], ["triangle", 2, 0.2, 0]], a: 0.03, s: 0.85, r: 0.1, cut: prog <= 67 ? 2600 : 2000, cutEnv: 2, vib: 7, lvl: 0.18 });
      case 9: return P({ w: [["sine", 1, 0.9, 0], ["triangle", 2, 0.12, 0], ["sine", 3, 0.05, 0]], a: 0.05, s: 0.9, r: 0.14, vib: 6, breath: 1, lvl: 0.34 });
      case 10: return P({ w: prog === 80 ? [["square", 1, 0.8, 0], ["square", 1, 0.5, 8]] : prog === 81 ? [["sawtooth", 1, 0.7, 0], ["sawtooth", 1, 0.5, 9]] : [["sawtooth", 1, 0.6, 0], ["square", prog === 86 ? 1.5 : 2, 0.4, 0]], a: 0.006, s: 0.8, r: 0.12, cut: 4200, q: 2, vib: 7, lvl: 0.15 });
      case 11: return P({ w: [["sawtooth", 1, 0.5, -12], ["sawtooth", 1, 0.5, 12], ["triangle", 0.5, 0.4, 0]], a: 0.5, s: 0.85, r: 1.2, cut: 1600, q: 1.2, vib: 4, lvl: 0.15 });
      case 12: return P({ w: [["sawtooth", 1, 0.5, 0], ["sine", 1.5, 0.4, 0], ["triangle", 2, 0.3, 7]], a: 0.15, d: 2, s: 0.6, r: 1.4, cut: 2400, cutEnv: 2, q: 5, vib: 5, lvl: 0.15 });
      case 13: if (prog === 109) return P({ w: [["sawtooth", 1, 0.6, 0], ["sawtooth", 1, 0.4, 5]], a: 0.04, s: 0.9, r: 0.1, cut: 2800, lvl: 0.16 });
               if (prog === 110) return P({ w: [["sawtooth", 1, 0.6, -6], ["sawtooth", 1, 0.6, 6]], a: 0.05, s: 0.85, r: 0.25, cut: 3200, vib: 10, lvl: 0.17 });
               if (prog === 111) return P({ w: [["square", 1, 0.7, 0], ["sawtooth", 2, 0.2, 0]], a: 0.03, s: 0.85, r: 0.1, cut: 2400, vib: 9, lvl: 0.17 });
               return P({ w: [["sawtooth", 1, 0.5, 0], ["triangle", 1, 0.6, 0], ["sine", 3, 0.15, 0]], a: 0.002, d: prog === 108 ? 0.9 : 1.6, s: 0, r: 0.25, cut: 3600, cutEnv: 1, lvl: 0.26 });
      case 14: if (prog === 112 || prog === 114) return P({ w: [["sine", 1, 1, 0], ["sine", 2, 0.4, 0], ["sine", prog === 114 ? 3.9 : 5.4, 0.2, 0]], a: 0.002, d: 1.4, s: 0, r: 0.3, lvl: 0.3 });
               return P({ w: [["sine", 1, 1, 0], ["triangle", 1.6, 0.2, 0]], a: 0.001, d: 0.5, s: 0, r: 0.15, lvl: 0.5, drop: 1 });
      default: return P({ noise: 1, w: [["sine", 1, 0.3, 0]], a: 0.2, s: 0.7, r: 0.8, cut: 1200, q: 3, lvl: 0.28 });
    }
  }
  tone(prog, pitch, vel, t, ch) {
    const ctx = this.ctx, P = this.patch(prog);
    const f = 440 * Math.pow(2, (pitch - 69) / 12);
    const v = Math.pow(vel / 127, 1.5) * P.lvl;
    const env = ctx.createGain(), relGain = ctx.createGain();
    env.connect(relGain);
    let head = env;
    if (P.cut) {
      const flt = ctx.createBiquadFilter(); flt.type = "lowpass"; flt.Q.value = P.q;
      const base = Math.min(16000, P.cut * (0.5 + vel / 127) * (P.piano ? Math.pow(f / 260, 0.3) : 1));
      if (P.cutEnv === 1) { flt.frequency.setValueAtTime(base, t); flt.frequency.setTargetAtTime(Math.max(base * 0.25, f * 1.5), t + 0.01, P.d / 5); }
      else if (P.cutEnv === 2) { flt.frequency.setValueAtTime(base * 0.3, t); flt.frequency.setTargetAtTime(base, t, P.a + 0.03); }
      else flt.frequency.value = base;
      flt.connect(env); head = flt;
    }
    if (P.drive) { const ws = ctx.createWaveShaper(); const c = new Float32Array(256); for (let i = 0; i < 256; i++) { const x = i / 128 - 1; c[i] = Math.tanh(x * 3); } ws.curve = c; ws.connect(head); head = ws; }
    const oscs = [];
    const d = P.s === 0 ? P.d * (1.5 - pitch / 127) : P.d;
    const naturalEnd = P.s === 0 ? t + P.a + d * 1.6 + 0.1 : 0;
    for (const [type, ratio, g, det] of P.w) {
      const o = ctx.createOscillator(); o.type = type; o.frequency.value = f * ratio;
      o._det = det; o.detune.value = ch.bend * 100 + det;
      if (P.drop) { o.frequency.setValueAtTime(f * ratio * 1.6, t); o.frequency.exponentialRampToValueAtTime(f * ratio, t + 0.08); }
      const og = ctx.createGain(); og.gain.value = g;
      o.connect(og).connect(head);
      o.start(t); if (naturalEnd) { o._stop = naturalEnd; o.stop(naturalEnd); }
      oscs.push(o);
    }
    if (P.vib) {
      const l = ctx.createOscillator(), lg = ctx.createGain(); l.frequency.value = 5.3; lg.gain.setValueAtTime(0, t); lg.gain.linearRampToValueAtTime(P.vib, t + 0.35);
      l.connect(lg); for (const o of oscs) lg.connect(o.detune);
      l.start(t); if (naturalEnd) { l._stop = naturalEnd; l.stop(naturalEnd); } oscs.push(l);
    }
    if (P.trem) {
      const l = ctx.createOscillator(), lg = ctx.createGain(); l.frequency.value = P.trem === 2 ? 12 : 4.5; lg.gain.value = 0.18;
      const tg = ctx.createGain(); tg.gain.value = 0.82; l.connect(lg).connect(tg.gain); relGain.disconnect(); env.disconnect(); env.connect(tg).connect(relGain);
      l.start(t); if (naturalEnd) { l._stop = naturalEnd; l.stop(naturalEnd); } oscs.push(l);
    }
    if (P.noise || P.breath) {
      const n = ctx.createBufferSource(); n.buffer = this.noise; n.loop = true; n.playbackRate.value = 0.5 + Math.random();
      const bp = ctx.createBiquadFilter(); bp.type = "bandpass"; bp.frequency.value = P.noise ? f * 2 : f * 3; bp.Q.value = P.noise ? 1.5 : 4;
      const ng = ctx.createGain(); ng.gain.value = P.noise ? 1.2 : 0.18;
      n.connect(bp).connect(ng).connect(env); n.start(t); if (naturalEnd) { n._stop = naturalEnd; n.stop(naturalEnd); } oscs.push(n);
    }
    env.gain.setValueAtTime(0, t);
    env.gain.linearRampToValueAtTime(v, t + P.a);
    env.gain.setTargetAtTime(v * P.s, t + P.a, Math.max(0.01, d / 4));
    // the main oscillator must be the one whose 'ended' fires last
    const main = oscs[0]; oscs.sort((a, b) => (a === main ? -1 : b === main ? 1 : 0));
    return { out: relGain, relGain, oscs, rel: P.r };
  }
  // ---- drums (channel 10)
  drum(note, vel, t, ch) {
    const ctx = this.ctx, v = Math.pow(vel / 127, 1.3) * 0.55;
    const out = ctx.createGain(), relGain = ctx.createGain(); out.connect(relGain);
    const oscs = [];
    const noise = (type, freq, q, a, dec, gain, rate) => {
      const n = ctx.createBufferSource(); n.buffer = this.noise; n.playbackRate.value = rate || 1;
      const f = ctx.createBiquadFilter(); f.type = type; f.frequency.value = freq; f.Q.value = q;
      const g = ctx.createGain(); g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(gain * v, t + a); g.gain.setTargetAtTime(0, t + a, dec / 4);
      n.connect(f).connect(g).connect(out); n.start(t, Math.random()); n.stop(t + a + dec * 1.3 + 0.05); oscs.push(n);
    };
    const tone = (type, f0, f1, sweep, dec, gain) => {
      const o = ctx.createOscillator(); o.type = type; o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(f1, t + sweep);
      const g = ctx.createGain(); g.gain.setValueAtTime(gain * v, t); g.gain.setTargetAtTime(0, t + 0.002, dec / 4);
      o.connect(g).connect(out); o.start(t); o.stop(t + dec * 1.3 + 0.05); oscs.push(o);
    };
    switch (note) {
      case 35: case 36: tone("sine", 160, 42, 0.11, 0.42, 1.6); tone("triangle", 400, 60, 0.02, 0.03, 0.5); break;
      case 37: noise("bandpass", 2200, 3, 0.001, 0.05, 1.2); tone("triangle", 820, 600, 0.02, 0.04, 0.5); break;
      case 38: case 40: noise("bandpass", 1900, 0.8, 0.001, 0.2, 1.3); tone("triangle", 230, 160, 0.05, 0.12, 0.8); break;
      case 39: for (let i = 0; i < 3; i++) { const tt = t + i * 0.011; const n = ctx.createBufferSource(); n.buffer = this.noise; const f = ctx.createBiquadFilter(); f.type = "bandpass"; f.frequency.value = 1300; f.Q.value = 1.4; const g = ctx.createGain(); g.gain.setValueAtTime(v * 1.4, tt); g.gain.setTargetAtTime(0, tt, i === 2 ? 0.04 : 0.006); n.connect(f).connect(g).connect(out); n.start(tt, Math.random()); n.stop(tt + 0.25); oscs.push(n); } break;
      case 42: case 44: noise("highpass", 7500, 0.8, 0.001, note === 44 ? 0.04 : 0.06, 0.8, 1.2); break;
      case 46: noise("highpass", 7000, 0.8, 0.002, 0.38, 0.7, 1.1); break;
      case 49: case 57: case 52: case 55: noise("highpass", 4800, 0.6, 0.002, 1.6, 0.65); noise("bandpass", 7600, 2, 0.002, 0.9, 0.3); break;
      case 51: case 59: case 53: noise("bandpass", 6800, 1.5, 0.001, 0.75, 0.45); tone("square", 5200, 5100, 0.05, 0.5, 0.04); break;
      case 41: case 43: case 45: case 47: case 48: case 50: { const f = 72 + (note - 41) * 18; tone("sine", f * 1.7, f, 0.2, 0.45, 1.2); noise("lowpass", 900, 1, 0.001, 0.06, 0.3); break; }
      case 54: noise("highpass", 9000, 1, 0.003, 0.2, 0.6); break;
      case 56: tone("square", 562, 560, 0.01, 0.25, 0.25); tone("square", 845, 840, 0.01, 0.2, 0.2); break;
      case 60: case 61: case 62: case 63: case 64: { const f = [420, 330, 360, 280, 210][note - 60]; tone("sine", f * 1.3, f, 0.03, note === 62 ? 0.08 : 0.25, 0.9); break; }
      case 65: case 66: tone("triangle", note === 65 ? 300 : 220, 200, 0.05, 0.3, 0.7); noise("bandpass", 3000, 1, 0.001, 0.12, 0.4); break;
      case 67: case 68: tone("sine", note === 67 ? 920 : 690, 680, 0.01, 0.3, 0.5); break;
      case 69: case 70: noise("highpass", 6000, 1, 0.008, 0.07, 0.6); break;
      case 75: tone("sine", 2500, 2400, 0.01, 0.06, 0.6); break;
      case 76: case 77: tone("sine", note === 76 ? 1200 : 900, 880, 0.01, 0.07, 0.7); break;
      case 80: case 81: tone("sine", 2800, 2790, 0.01, note === 80 ? 0.1 : 0.9, 0.3); tone("sine", 4300, 4300, 0.01, note === 80 ? 0.08 : 0.7, 0.15); break;
      default: noise("bandpass", 2400, 1, 0.001, 0.1, 0.6); break;
    }
    out.gain.value = 1;
    return { out: relGain, relGain, oscs, rel: 0.6 };
  }
}

/* ===================================================== Player */
const Player = {
  ctx: null, synth: null, song: null, meta: null,
  playing: false, startCtx: 0, startPos: 0, pos: 0, speed: 1, transpose: 0, idx: 0,
  mute: new Array(16).fill(false), solo: new Array(16).fill(false),
  voiceMap: new Map(), midiOut: null, timer: 0, onEnd: null, onProgram: null, progNow: new Array(16).fill(0),
  ensureCtx() {
    if (!this.ctx) {
      const AC = window.AudioContext || window.webkitAudioContext;
      this.ctx = new AC({ latencyHint: "interactive" });
      this.synth = new Synth(this.ctx);
      this.synth.master.gain.value = Math.pow(store.get("vol", 80) / 100, 1.5);
      unlockIOS();
    }
    if (this.ctx.state !== "running") this.ctx.resume();
  },
  load(song, meta) {
    this.stop(true);
    this.song = song; this.meta = meta; this.pos = 0; this.idx = 0;
    this.mute.fill(false); this.solo.fill(false);
    this.progNow = song.firstProg.map(p => Math.max(0, p));
  },
  time() { return this.playing ? this.startPos + (this.ctx.currentTime - this.startCtx) * this.speed : this.pos; },
  audible(ch) { return this.solo.some(Boolean) ? this.solo[ch] : !this.mute[ch]; },
  play() {
    if (!this.song) return;
    this.ensureCtx();
    if (this.pos >= this.song.duration - 0.05) this.pos = 0;
    this.chase(this.pos);
    this.startCtx = this.ctx.currentTime + 0.06; this.startPos = this.pos;
    this.playing = true;
    clearInterval(this.timer); this.timer = setInterval(() => this.pump(), 25); this.pump();
  },
  pause() { if (!this.playing) return; this.pos = this.time(); this.playing = false; clearInterval(this.timer); this.silence(); },
  stop(keepPos) { this.playing = false; clearInterval(this.timer); this.silence(); if (!keepPos) this.pos = 0; },
  seek(t) {
    const was = this.playing; if (was) { this.playing = false; clearInterval(this.timer); this.silence(); }
    this.pos = Math.max(0, Math.min(t, this.song ? this.song.duration : 0));
    if (was) this.play();
  },
  setSpeed(s) { if (this.playing) { this.startPos = this.time(); this.startCtx = this.ctx.currentTime; } this.speed = s; },
  silence() {
    if (this.synth) this.synth.allOff(false);
    this.voiceMap.clear();
    if (this.midiOut) { for (let c = 0; c < 16; c++) { this.midiOut.send([0xb0 | c, 64, 0]); this.midiOut.send([0xb0 | c, 123, 0]); } }
  },
  chase(t) {   // rebuild program/controller state up to time t
    const ev = this.song.events; let i = 0;
    if (this.synth) this.synth.reset();
    const now = this.ctx.currentTime;
    for (; i < ev.length && ev[i].t < t; i++) {
      const e = ev[i];
      if (e.st === 0x90 || e.st === 0x80 || e.st === 0xa0) continue;
      this.dispatch(e, now, true);
    }
    this.idx = i;
  },
  dispatch(e, when, quiet) {
    const s = this.synth, mo = this.midiOut;
    if (e.st === 0xc0) { this.progNow[e.ch] = e.a; if (s) s.program(e.ch, e.a); if (this.onProgram) this.onProgram(e.ch, e.a); }
    else if (e.st === 0xb0) { if (s) s.control(e.ch, e.a, e.b, when); }
    else if (e.st === 0xe0) { if (s) s.pitchBend(e.ch, (e.b << 7) | e.a, when); }
    if (mo && e.st !== 0x90 && e.st !== 0x80) {
      const msg = e.st === 0xc0 || e.st === 0xd0 ? [e.st | e.ch, e.a] : [e.st | e.ch, e.a, e.b];
      mo.send(msg, quiet ? undefined : this.perfTime(when));
    }
  },
  perfTime(ctxTime) { return performance.now() + (ctxTime - this.ctx.currentTime) * 1000; },
  pump() {
    if (!this.playing) return;
    const ctx = this.ctx, ev = this.song.events, ahead = 0.18;
    const horizon = this.startPos + (ctx.currentTime + ahead - this.startCtx) * this.speed;
    while (this.idx < ev.length && ev[this.idx].t <= horizon) {
      const e = ev[this.idx++];
      const when = this.startCtx + (e.t - this.startPos) / this.speed;
      const key = e.ch * 128 + e.a;
      if (e.st === 0x90 && e.b > 0) {
        if (!this.audible(e.ch)) continue;
        const p = e.ch === 9 ? e.a : Math.max(0, Math.min(127, e.a + this.transpose));
        if (this.midiOut) { this.midiOut.send([0x90 | e.ch, p, e.b], this.perfTime(when)); this.voiceMap.set(key, (this.voiceMap.get(key) || []).concat([{ midi: p }])); }
        else { const vo = this.synth.noteOn(e.ch, p, e.b, when); if (vo) { const arr = this.voiceMap.get(key) || []; arr.push(vo); this.voiceMap.set(key, arr); } }
      } else if (e.st === 0x80 || e.st === 0x90) {
        const arr = this.voiceMap.get(key); if (!arr || !arr.length) continue;
        const vo = arr.shift();
        if (e.ch === 9 && vo.midi === undefined) continue;   // GM drums ring out; ignore note-off
        if (vo.midi !== undefined) { if (this.midiOut) this.midiOut.send([0x80 | e.ch, vo.midi, 0], this.perfTime(when)); }
        else this.synth.noteOff(vo, when);
      } else this.dispatch(e, when);
    }
    if (this.time() >= this.song.duration) { this.stop(); this.pos = this.song.duration; if (this.onEnd) this.onEnd(); }
  },
  setChannelAudible() { if (!this.playing) return; for (let c = 0; c < 16; c++) if (!this.audible(c) && this.synth) for (const vo of [...this.synth.ch[c].voices]) this.synth.release(vo, this.ctx.currentTime, true); }
};

// iPhone: play a silent <audio> loop so Web Audio isn't muted by the ring/silent switch
let iosUnlocked = false;
function unlockIOS() {
  if (iosUnlocked) return; iosUnlocked = true;
  try {
    const sr = 8000, n = 800, b = new ArrayBuffer(44 + n * 2), v = new DataView(b);
    const w = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
    w(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); w(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); w(36, "data"); v.setUint32(40, n * 2, true);
    const a = new Audio(URL.createObjectURL(new Blob([b], { type: "audio/wav" }))); a.loop = true; a.setAttribute("playsinline", ""); a.volume = 0.01; a.play().catch(() => {});
  } catch (e) {}
}


// ---------------------------------------------------------------------------------------------
// The Shelf: make the Music player's <audio id="audioPlayer"> play MIDI files too. When a
// .mid/.midi/.kar/.rmi file is set as its src, the synth above plays it, and the element still
// answers play()/pause()/currentTime/duration/volume and fires timeupdate/ended, so The
// Shelf's own controls, seek bar, next-song and lock-screen buttons work unchanged.
const A = document.getElementById('audioPlayer');
if (!A) return;
const MP = HTMLMediaElement.prototype;
const nat = n => Object.getOwnPropertyDescriptor(MP, n);
const isMidi = u => { try { return /\.(mid|midi|kar|rmi)$/i.test(decodeURIComponent(new URL(String(u || ''), location.href).pathname)); } catch { return false; } };
let midi = false, midiSrc = '', seq = 0, pendingPlay = false, tick = 0, vol = 1;
const fire = t => A.dispatchEvent(new Event(t));
const ticks = on => { clearInterval(tick); if (on) tick = setInterval(() => { if (midi && Player.playing) fire('timeupdate'); }, 250); };
const applyVol = () => { if (Player.synth) Player.synth.master.gain.value = 0.8 * Math.pow(vol, 1.5); };

// Use The Shelf's own AudioContext when it has one, feeding the visualizer's analysers, so the
// bars move for MIDI too (on iPhone the visualizer works differently; there it just plays).
Player.ensureCtx = function () {
  if (!this.ctx) {
    try { if (typeof ensureVisualizer === 'function') ensureVisualizer(); } catch (e) {}
    let ctx = null, out = [];
    try { if (!(typeof IS_IOS !== 'undefined' && IS_IOS) && typeof visualizerCtx !== 'undefined' && visualizerCtx && visualizerAnalyser) { ctx = visualizerCtx; out = [visualizerAnalyser]; if (visualizerScopeAnalyser) out.push(visualizerScopeAnalyser); } } catch (e) {}
    if (!ctx) { const AC = window.AudioContext || window.webkitAudioContext; ctx = new AC({ latencyHint: 'interactive' }); }
    this.ctx = ctx;
    this.synth = new Synth(ctx);
    if (out.length) { this.synth.comp.disconnect(); for (const n of out) this.synth.comp.connect(n); }   // analyser -> speakers
    applyVol();
    unlockIOS();
  }
  if (this.ctx.state !== 'running') this.ctx.resume();
};
Player.onEnd = () => { if (!midi) return; ticks(false); fire('timeupdate'); fire('ended'); };

const start = () => { Player.play(); fire('play'); fire('playing'); ticks(true); };
Object.defineProperty(A, 'src', {
  configurable: true,
  get() { return midi ? midiSrc : nat('src').get.call(A); },
  set(v) {
    if (isMidi(v)) {
      if (!midi) { try { MP.pause.call(A); A.removeAttribute('src'); MP.load.call(A); } catch (e) {} }   // stop the real player quietly
      Player.stop(); ticks(false);
      midi = true; midiSrc = new URL(v, location.href).href; Player.song = null; pendingPlay = false;
      const my = ++seq;
      fetch(midiSrc).then(r => { if (!r.ok) throw new Error(r.status); return r.arrayBuffer(); }).then(buf => {
        if (my !== seq) return;
        Player.load(parseMidi(buf, decodeURIComponent(midiSrc.split('/').pop())), {});
        fire('loadedmetadata'); fire('durationchange'); fire('canplay');
        if (pendingPlay) { pendingPlay = false; start(); }
      }).catch(e => { if (my !== seq) return; console.warn('MIDI: could not play', midiSrc, e); fire('error'); setTimeout(() => { if (my === seq) fire('ended'); }, 800); });   // unreadable: skip to the next song
    } else {
      if (midi) { Player.stop(); ticks(false); midi = false; midiSrc = ''; seq++; pendingPlay = false; }
      nat('src').set.call(A, v);
    }
  }
});
A.play = function () {
  if (!midi) return MP.play.call(A);
  Player.ensureCtx();                              // inside the tap, so phones allow the sound
  if (!Player.song) { pendingPlay = true; return Promise.resolve(); }
  start();
  return Promise.resolve();
};
A.pause = function () {
  if (!midi) return MP.pause.call(A);
  pendingPlay = false;
  if (Player.playing) { Player.pause(); ticks(false); fire('pause'); }
};
Object.defineProperty(A, 'paused', { configurable: true, get() { return midi ? !(Player.playing || pendingPlay) : nat('paused').get.call(A); } });
Object.defineProperty(A, 'ended', { configurable: true, get() { return midi ? !!(Player.song && !Player.playing && Player.pos >= Player.song.duration) : nat('ended').get.call(A); } });
Object.defineProperty(A, 'duration', { configurable: true, get() { return midi ? (Player.song ? Player.song.duration : NaN) : nat('duration').get.call(A); } });
Object.defineProperty(A, 'currentTime', {
  configurable: true,
  get() { return midi ? (Player.song ? Math.max(0, Math.min(Player.time(), Player.song.duration)) : 0) : nat('currentTime').get.call(A); },
  set(t) { if (!midi) return nat('currentTime').set.call(A, t); if (Player.song) { Player.seek(+t || 0); fire('seeked'); fire('timeupdate'); } }
});
Object.defineProperty(A, 'volume', {
  configurable: true,
  get() { return nat('volume').get.call(A); },
  set(v) { nat('volume').set.call(A, v); vol = Math.max(0, Math.min(1, +v)); applyVol(); }
});
try { vol = nat('volume').get.call(A); } catch (e) {}
window.shelfMidiPlayer = Player;

})();
