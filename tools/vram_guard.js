#!/usr/bin/env node
/*
 * vram_guard.js - static VRAM tile-map collision guard for the MDX-4 DX-7 skin.
 *
 * Scans every src/ui/*.c file, reconstructs the VRAM tile ranges that get loaded
 * via VDP_loadTileData(), and enforces three invariants:
 *
 *   (a) NO OVERLAP      - no two reserved ranges may intersect
 *                          (touching end==start is allowed: a shared boundary).
 *   (b) NO NEW TILE BELOW 0x019C
 *                        - every tile introduced by THIS skin work (the W0-T2..T6
 *                          widgets, i.e. any MDXGFX_RD_/KNOB_/FADER_/LED_/BTN_/SEL_/
 *                          SHELL_ tile address) MUST sit at >= 0x019C. The three
 *                          pre-existing blocks 0x0130..0x019B (W2 / W3-envelope /
 *                          W5-mini) Pre-EXIST this skin work and are EXEMPT: they
 *                          are retained, not "new", so the guard does not flag them
 *                          even though they sit below 0x019C.
 *   (c) TAIL UNDER HARDWARE CEILING
 *                        - the aggregate of all tiles reserved in the >=0x019C tail
 *                          must stay under the TRUE 612-tile hardware ceiling
 *                          (0x019C..0x03FF inclusive = 612 tiles). 612 is the real
 *                          Mega Drive VRAM limit for this region, not the plan's
 *                          soft 100-tile design sub-budget.
 *
 * On success prints the per-widget ranges, the aggregate >=0x019C total and the
 * new-skin high-water mark, and exits 0. On any violation it prints a clear
 * message and exits 1.
 *
 * The guard derives ranges from the ACTUAL load sites, so a future regression
 * (a new widget overlapping an existing one, a skin tile allocated below 0x019C,
 * or the tail exceeding 612) fails the build gate. It does not need a host C
 * compiler - only Node, which is present on this toolchain (see tools/pad_rom.js).
 *
 * Usage:  node tools/vram_guard.js
 */
'use strict';

const fs = require('fs');
const path = require('path');

const UI_DIR = path.join(__dirname, '..', 'src', 'ui');

/* First tile the Wave-0 skin is allowed to claim, and the true hardware ceiling
 * (number of tiles in 0x019C..0x03FF inclusive). */
const NEW_SKIN_BASE = 0x019C;
const TAIL_CEILING_TILES = 612;        // 0x03FF - 0x019C + 1
const VRAM_MAX = 0x0400;               // exclusive upper bound of the tile index

/* Widget classifier: a define-name prefix -> {label, skin}. `skin:true` marks a
 * tile introduced by this skin work (subject to rule b). Order matters: the
 * first matching prefix wins. */
const WIDGET_RULES = [
  // --- new-skin widgets (W0-T2..T6) : must be >= 0x019C ---
  { re: /^MDXGFX_RD_/,        label: 'Readout (W0-T2)',          skin: true  },
  { re: /^MDXGFX_SHELL/,      label: 'DeviceShell (W0-T6)',      skin: true  },
  { re: /^MDXGFX_KNOB/,       label: 'Controls (W0-T3/T4/T5)',   skin: true  },
  { re: /^MDXGFX_FADER/,      label: 'Controls (W0-T3/T4/T5)',   skin: true  },
  { re: /^MDXGFX_LED/,        label: 'Controls (W0-T3/T4/T5)',   skin: true  },
  { re: /^MDXGFX_BTN/,        label: 'Controls (W0-T3/T4/T5)',   skin: true  },
  { re: /^MDXGFX_SEL/,        label: 'Controls (W0-T3/T4/T5)',   skin: true  },
  // --- pre-existing owned ranges (exempt from the <0x019C new-tile check) ---
  { re: /^MDXGFX_TILE_/,      label: 'MdxGfx base tiles',         skin: false },
  { re: /^ALG_TILE_/,         label: 'W2 algorithm routing',      skin: false },
  { re: /^GRAPH_TILE_/,       label: 'W3 envelope graph',        skin: false },
  { re: /^MINI_/,             label: 'W5 mini routing diagram',   skin: false },
  { re: /^CURSOR_TILE_INDEX/, label: 'Sprite cursor',             skin: false },
  { re: /^BLANK_TILE_INDEX/,  label: 'Blank wipe tile',           skin: false },
];

function classify(name) {
  for (const r of WIDGET_RULES) {
    if (r.re.test(name)) return { label: r.label, skin: r.skin };
  }
  return { label: 'other (' + name + ')', skin: false };
}

/* ----------------------------------------------------------------------- */
/* Expression helpers.                                                       */
/* ----------------------------------------------------------------------- */

/* Replace C casts and string/comment noise with length-preserving spaces so
 * offsets/line numbers survive, but (here) we only need a clean expression. We
 * strip casts entirely since they carry no numeric meaning for this guard. */
function cleanExpr(s) {
  return s
    .replace(/\(\s*\(?\s*(?:unsigned\s+|signed\s+)?(?:u?int|u?\d+_t|short|long|char|bool|s\d+|u\d+)\s*\)?\s*\)/g, ' ') // (u16), (unsigned int), ...
    .replace(/\(\s*(?:unsigned\s+|signed\s+)?(?:u?int|u?\d+_t|short|long|char|bool|s\d+|u\d+)\s*\)/g, ' ')
    .trim();
}

/* Evaluate a small arithmetic expression (+, -, *, /, parentheses, hex/dec).
 * Identifiers must already be substituted for their integer values. */
function evalArith(input) {
  const s = input.replace(/\s+/g, '');
  let pos = 0;

  function num() {
    if (s[pos] === '0' && (s[pos + 1] === 'x' || s[pos + 1] === 'X')) {
      pos += 2;
      let h = '';
      while (pos < s.length && /[0-9A-Fa-f]/.test(s[pos])) h += s[pos++];
      return parseInt(h, 16);
    }
    let n = '';
    while (pos < s.length && /[0-9]/.test(s[pos])) n += s[pos++];
    if (n === '') throw new Error('expected number in "' + input + '"');
    return parseInt(n, 10);
  }
  function factor() {
    if (s[pos] === '(') { pos++; const v = expr(); if (s[pos] !== ')') throw new Error('expected ) in "' + input + '"'); pos++; return v; }
    return num();
  }
  function term() {
    let v = factor();
    while (s[pos] === '*' || s[pos] === '/') {
      const op = s[pos++];
      const r = factor();
      v = op === '*' ? v * r : Math.trunc(v / r);
    }
    return v;
  }
  function expr() {
    let v = term();
    while (s[pos] === '+' || s[pos] === '-') {
      const op = s[pos++];
      const r = term();
      v = op === '+' ? v + r : v - r;
    }
    return v;
  }
  const v = expr();
  if (pos !== s.length) throw new Error('trailing chars in "' + input + '"');
  return v;
}

/* Resolve an arbitrary expression to an integer. `lookup` resolves an identifier
 * (define name or local var) to its (possibly symbolic) defining expression. */
function resolveValue(raw, lookup, cache) {
  // Symbol tables (enum values) may hand back a number directly.
  if (typeof raw === 'number') return raw;
  let exprStr = cleanExpr(raw);
  const cacheKey = exprStr;
  if (cache.has(cacheKey)) return cache.get(cacheKey);

  let changed = true;
  let guard = 0;
  while (changed) {
    changed = false;
    exprStr = exprStr.replace(/\b([A-Za-z_]\w*)\b/g, (m, id) => {
      const sub = lookup(id);
      if (sub === null || sub === undefined) return m;
      if (typeof sub === 'number') { changed = true; return '(' + sub + ')'; }
      const v = resolveValue(sub, lookup, cache);
      changed = true;
      return '(' + v + ')';
    });
    if (++guard > 200) throw new Error('resolveValue not converging: ' + raw);
  }
  const val = evalArith(exprStr);
  cache.set(cacheKey, val);
  return val;
}

/* ----------------------------------------------------------------------- */
/* Source scanning.                                                          */
/* ----------------------------------------------------------------------- */

/* Strip comments to spaces while preserving newlines (keeps line numbers). */
function stripComments(src) {
  let out = '';
  let i = 0;
  const n = src.length;
  while (i < n) {
    const c = src[i];
    const d = src[i + 1];
    if (c === '/' && d === '/') {
      while (i < n && src[i] !== '\n') { out += ' '; i++; }
    } else if (c === '/' && d === '*') {
      out += '  '; i += 2;
      while (i < n && !(src[i] === '*' && src[i + 1] === '/')) {
        out += src[i] === '\n' ? '\n' : ' ';
        i++;
      }
      if (i < n) { out += '  '; i += 2; }
    } else if (c === '"' || c === "'") {
      out += c; i++;
      while (i < n && src[i] !== c) {
        if (src[i] === '\\' && i + 1 < n) { out += src[i] + src[i + 1]; i += 2; }
        else { out += src[i]; i++; }
      }
      if (i < n) { out += c; i++; }
    } else {
      out += c; i++;
    }
  }
  return out;
}

/* Parse `#define NAME EXPR` into a name->expr map (object-like macros only). */
function parseDefines(text) {
  const out = {};
  const re = /^#\s*define\s+([A-Za-z_]\w*)\s+([^\n]+)/gm;
  let m;
  while ((m = re.exec(text)) !== null) {
    out[m[1]] = m[2].trim();
  }
  return out;
}

/* Parse `typedef enum { A = 0, B, C = 5, ... }` members into a name->value map.
 * Members without an explicit value auto-increment from the running counter. The
 * MdxGfx base-tile offsets (MDXGFX_TILE_FILL=0 .. MDXGFX_TILE_KEY_BLACK=12) come
 * from an enum, not a #define, so the guard must capture them too. */
function parseEnums(text) {
  const out = {};
  const enumRe = /typedef\s+enum\s*\{/g;
  let m;
  while ((m = enumRe.exec(text)) !== null) {
    let i = m.index + m[0].length;
    let depth = 1;
    while (i < text.length && depth > 0) {
      if (text[i] === '{') depth++;
      else if (text[i] === '}') { depth--; if (depth === 0) break; }
      i++;
    }
    const body = text.slice(m.index + m[0].length, i);
    let counter = 0;
    for (const raw of body.split(',')) {
      const mem = raw.trim();
      if (mem === '') continue;
      const eq = mem.indexOf('=');
      let name, val;
      if (eq >= 0) {
        name = mem.slice(0, eq).trim();
        const sv = mem.slice(eq + 1).trim();
        val = sv.startsWith('0x') ? parseInt(sv, 16) : parseInt(sv, 10);
        if (isNaN(val)) val = counter;
        else counter = val;
      } else {
        name = mem;
        val = counter;
      }
      if (/^[A-Za-z_]\w*$/.test(name)) out[name] = val;
      counter = val + 1;
    }
  }
  return out;
}

/* Find every local `const TYPE name = EXPR;` assignment in the text and return
 * an array of {name, expr, line}. Used to resolve loop-var locals (idx, base). */
function parseLocalAssignments(text) {
  const out = [];
  const re = /\b(?:const\s+)?(?:u8|u16|u32|s8|s16|s32|int)\s+([A-Za-z_]\w*)\s*=\s*([^;\n]+);/g;
  let m;
  while ((m = re.exec(text)) !== null) {
    out.push({ name: m[1], expr: m[2].trim(), line: text.slice(0, m.index).split('\n').length });
  }
  return out;
}

/* Find every `for (VAR = LO; VAR < HI; VAR++)` and the following brace body,
 * returning {var, lo, hiExpr, bodyStart, bodyEnd}. hiExpr is resolved lazily. */
function parseLoops(text, defines) {
  const loops = [];
  const re = /\bfor\s*\(\s*(?:unsigned\s+)?(?:u8|u16|u32|int\s+)?\s*([A-Za-z_]\w*)\s*=\s*(\d+)\s*;\s*\1\s*<\s*([^;]+?)\s*;\s*\1\s*\+\+\s*\)/g;
  let m;
  while ((m = re.exec(text)) !== null) {
    const headEnd = m.index + m[0].length;
    // Only a brace-bodied loop can enclose a VDP_loadTileData site. A loop with no
    // '{' (single statement or the outer of a nested non-braced for) owns no brace
    // span and must be skipped, otherwise it can wrongly hijack enclosure detection
    // for the next real brace block (e.g. `for (i=0;i<8) t[i]=0;` latching onto the
    // later `{ ... }` that actually belongs to `for (i=0;i<41)`).
    let i = headEnd;
    while (i < text.length && (text[i] === ' ' || text[i] === '\t' || text[i] === '\n' || text[i] === '\r')) i++;
    if (text[i] !== '{') continue;
    const bodyStart = i + 1;
    let depth = 0;
    let j = i;
    for (; j < text.length; j++) {
      if (text[j] === '{') depth++;
      else if (text[j] === '}') { depth--; if (depth === 0) break; }
    }
    const bodyEnd = j; // index of closing brace
    loops.push({
      var: m[1],
      lo: parseInt(m[2], 10),
      hiExpr: m[3].trim(),
      bodyStart,
      bodyEnd,
    });
  }
  return loops;
}

/* Extract the comma-separated args of a VDP_loadTileData(...) call whose opening
 * paren is at text[openIdx]. Returns array of raw arg strings. */
function extractArgs(text, openIdx) {
  let depth = 1;   // the opening paren at openIdx is already consumed -> depth 1 inside
  let i = openIdx + 1;
  const args = [];
  let cur = '';
  for (; i < text.length; i++) {
    const ch = text[i];
    if (ch === '(') { depth++; cur += ch; }
    else if (ch === ')') {
      depth--;
      if (depth === 0) { args.push(cur); break; }
      cur += ch;
    } else if (ch === ',' && depth === 1) { args.push(cur); cur = ''; }
    else cur += ch;
  }
  // drop the trailing element that we pushed on ')'
  args.pop();
  return args;
}

/* ----------------------------------------------------------------------- */
/* Main analysis.                                                            */
/* ----------------------------------------------------------------------- */

function lineOf(text, index) {
  let line = 1;
  for (let i = 0; i < index && i < text.length; i++) if (text[i] === '\n') line++;
  return line;
}

function analyzeFile(file, defines, cache, reservations, errors) {
  const raw = fs.readFileSync(file, 'utf8');
  const cleaned = stripComments(raw);
  // Tile-address defines live in the .h files; the symbol table is built globally
  // (all *.c + *.h) by run(), so here we only need this file's own local vars.
  const locals = parseLocalAssignments(cleaned);
  const loops = parseLoops(cleaned, defines);

  // Lookup closure: global define first, then file-local assignment (nearest, at
  // or above the reference line).
  function lookup(id, nearLine) {
    if (Object.prototype.hasOwnProperty.call(defines, id)) return defines[id];
    let best = null;
    for (const a of locals) {
      if (a.name === id && (nearLine === undefined || a.line <= nearLine)) {
        if (!best || a.line > best.line) best = a;
      }
    }
    return best ? best.expr : null;
  }
  function resolve(expr, nearLine) {
    return resolveValue(expr, (id) => lookup(id, nearLine), cache);
  }

  // Find all VDP_loadTileData call sites.
  const callRe = /VDP_loadTileData\s*\(/g;
  let cm;
  while ((cm = callRe.exec(cleaned)) !== null) {
    const openIdx = cm.index + cm[0].length - 1;
    const args = extractArgs(cleaned, openIdx);
    if (args.length < 2) continue;
    const addrRaw = args[1].trim();
    const callLine = lineOf(cleaned, cm.index);

    // Resolve local variables (idx, base, ...) in the address expression by
    // substituting their defining expression.
    let addrExpr = addrRaw;
    let localGuard = 0;
    let substituted = true;
    while (substituted) {
      substituted = false;
      addrExpr = addrExpr.replace(/\b([A-Za-z_]\w*)\b/g, (mm, id) => {
        let best = null;
        for (const a of locals) {
          if (a.name === id && a.line <= callLine) {
            if (!best || a.line > best.line) best = a;
          }
        }
        if (best) { substituted = true; return '(' + best.expr + ')'; }
        return mm;
      });
      if (++localGuard > 100) break;
    }
    addrExpr = cleanExpr(addrExpr);

    // Determine whether the address references an enclosing for-loop variable.
    let enclosing = null;
    for (const lp of loops) {
      if (lp.bodyStart <= cm.index && cm.index < lp.bodyEnd) {
        if (!enclosing || lp.bodyStart > enclosing.bodyStart) enclosing = lp;
      }
    }

    let start, end, label, isSkin, anchorExpr = addrExpr;

    if (enclosing && new RegExp('\\b' + enclosing.var + '\\b').test(addrExpr)) {
      // Range load: addr = BASE [+-] LOOPVAR  (or LOOPVAR [+-] BASE).
      const re = new RegExp('\\(\\s*([^+\\-]+?)\\s*([+-])\\s*' + enclosing.var + '\\s*\\)|\\b' + enclosing.var + '\\s*([+-])\\s*([^+\\-]+?)\\b');
      const rm = re.exec(addrExpr);
      let baseExpr, bound;
      if (rm) {
        if (rm[1] !== undefined) { baseExpr = rm[1]; bound = rm[2] === '+' ? 1 : -1; }
        else { baseExpr = rm[4]; bound = rm[3] === '+' ? -1 : 1; }
      } else {
        // fall back: anything that is not the loop var is the base
        baseExpr = addrExpr.replace(new RegExp('\\(\\s*' + enclosing.var + '\\s*\\)|\\b' + enclosing.var + '\\b', 'g'), '').replace(/^[+\-]|[+\-]$/g, '').trim();
        bound = 1;
      }
      const lo = enclosing.lo;
      const hi = resolve(enclosing.hiExpr, callLine);
      const baseVal = resolve(baseExpr, callLine);
      start = baseVal + lo;
      end = baseVal + hi; // exclusive
      anchorExpr = baseExpr;
    } else {
      // Single tile load.
      const v = resolve(addrExpr, callLine);
      start = v;
      end = v + 1;
    }

    // Classify by anchoring define name(s) in the expression.
    const idRe = /\b([A-Za-z_]\w*)\b/g;
    let im;
    let chosen = { label: 'other', skin: false };
    while ((im = idRe.exec(anchorExpr)) !== null) {
      if (Object.prototype.hasOwnProperty.call(defines, im[1])) {
        const c = classify(im[1]);
        if (c.skin) { chosen = c; break; }            // skin wins
        if (chosen.label === 'other') chosen = c;     // keep first non-other
      }
    }
    label = chosen.label;
    isSkin = chosen.skin;

    if (!Number.isFinite(start) || !Number.isFinite(end)) {
      errors.push(file + ':' + callLine + ' could not resolve VRAM address: "' + addrRaw + '"');
      continue;
    }

    reservations.push({ file, line: callLine, start, end, label, isSkin });
  }
}

/* ----------------------------------------------------------------------- */
/* Validation + reporting.                                                   */
/* ----------------------------------------------------------------------- */

function run() {
  // Tile-address defines (MDXGFX_*, ALG_TILE_*, GRAPH_TILE_*, MINI_*, CURSOR_/
  // BLANK_TILE_INDEX) live in the .h headers; the load sites that reference them
  // live in the .c files. Build ONE global symbol table from every .c and .h so a
  // .c file's VDP_loadTileData sites can resolve their address constants.
  const allSources = fs.readdirSync(UI_DIR)
    .filter((f) => f.endsWith('.c') || f.endsWith('.h'))
    .map((f) => path.join(UI_DIR, f))
    .sort();

  const defines = {};
  for (const f of allSources) {
    const cleaned = stripComments(fs.readFileSync(f, 'utf8'));
    Object.assign(defines, parseDefines(cleaned));
    Object.assign(defines, parseEnums(cleaned));
  }

  const cFiles = allSources.filter((f) => f.endsWith('.c'));

  const reservations = [];
  const errors = [];
  const cache = new Map();

  for (const f of cFiles) analyzeFile(f, defines, cache, reservations, errors);

  const fail = (msg) => { console.error('VRAM GUARD FAIL: ' + msg); };

  // --- (c) tiles must not exceed VRAM ---
  for (const r of reservations) {
    if (r.end > VRAM_MAX) {
      fail('reserved range ' + fmt(r) + ' (in ' + r.file + ') exceeds VRAM max 0x' + VRAM_MAX.toString(16));
      errors.push('vr');
    }
  }

  // --- (a) no overlap (touching boundaries allowed) ---
  const sorted = reservations.slice().sort((a, b) => a.start - b.start);
  let maxEnd = -1, maxLabel = null, maxStart = -1, maxFile = null;
  for (const iv of sorted) {
    if (iv.start < maxEnd) {
      fail('reserved range ' + fmt(iv) + ' (' + iv.label + ') overlaps ' +
           '[' + hex(maxStart) + '..' + hex(maxEnd) + ') (' + maxLabel + ', ' + maxFile + ')');
      errors.push('overlap');
    }
    if (iv.end > maxEnd) { maxEnd = iv.end; maxLabel = iv.label; maxStart = iv.start; maxFile = iv.file; }
  }

  // --- (b) no NEW skin tile below 0x019C ---
  for (const r of reservations) {
    if (r.isSkin && r.start < NEW_SKIN_BASE) {
      fail('NEW skin tile at ' + hex(r.start) + ' (' + r.label + ', ' + r.file + ':' + r.line +
           ') is below the 0x019C new-skin floor');
      errors.push('below019c');
    }
  }

  // --- (c) aggregate >=0x019C under the 612-tile ceiling ---
  let tailCount = 0;
  let highWater = NEW_SKIN_BASE;
  for (const r of reservations) {
    if (r.start >= NEW_SKIN_BASE) tailCount += (r.end - r.start);
    if (r.isSkin) highWater = Math.max(highWater, r.end);
  }
  if (tailCount > TAIL_CEILING_TILES) {
    fail('>=0x019C skin tail reserves ' + tailCount + ' tiles, exceeding the 612-tile hardware ceiling');
    errors.push('ceiling');
  }

  // --- report ---
  console.log('VRAM GUARD: scanned ' + cFiles.length + ' .c file(s) in ' + path.relative(process.cwd(), UI_DIR));
  console.log('');
  console.log('Per-widget reserved ranges (start..end-excl [count]):');
  const byLabel = {};
  for (const r of sorted) {
    (byLabel[r.label] = byLabel[r.label] || []).push(r);
  }
  for (const label of Object.keys(byLabel)) {
    // merge contiguous per-label intervals for a clean line
    const ivs = byLabel[label].slice().sort((a, b) => a.start - b.start);
    const merged = [];
    for (const iv of ivs) {
      const last = merged[merged.length - 1];
      if (last && iv.start <= last.end) last.end = Math.max(last.end, iv.end);
      else merged.push({ start: iv.start, end: iv.end });
    }
    let total = 0;
    const parts = merged.map((m) => { total += m.end - m.start; return hex(m.start) + '..' + hex(m.end); });
    console.log('  ' + pad(label, 28) + parts.join(', ') + '  [' + total + ']');
  }
  console.log('');
  console.log('  aggregate >=0x019C tiles : ' + tailCount + ' / ' + TAIL_CEILING_TILES + ' (hardware ceiling)');
  console.log('  new-skin high-water mark : ' + hex(highWater) + ' (exclusive)');

  if (errors.length) {
    console.error('');
    console.error('VRAM GUARD: FAILED with ' + errors.length + ' issue(s).');
    process.exit(1);
  }
  console.log('');
  console.log('VRAM GUARD: OK - no collisions, skin tail within ceiling.');
  process.exit(0);
}

function hex(n) { return '0x' + Number(n).toString(16).toUpperCase().padStart(4, '0'); }
function fmt(r) { return '[' + hex(r.start) + '..' + hex(r.end) + ')'; }
function pad(s, n) { return (s + '                          ').slice(0, n); }

run();
