// SketchyUp UX mockups: static illustrations of docs/UX_DESIGN.md, not application code.
// Open index.html?screen=draw|context|assistant|render|narrow
(() => {

// --- Scene: the M5 demonstration room (6 m x 4 m, 0.2 m walls, 2.7 m high) ---
const S = 70, C = 0.866, H = 2.7;
const P = (x, y, z = 0) => [(x - y) * C * S, (x + y) * 0.5 * S - z * S];
const pts = a => a.map(p => P(...p).map(n => n.toFixed(1)).join(',')).join(' ');
const poly = (a, cls, extra = '') => `<polygon points="${pts(a)}" class="${cls}" ${extra}/>`;
const seg = (a, b, cls) => `<polyline points="${pts([a, b])}" class="${cls}"/>`;
const rectY = (x1, x2, z1, z2, y) => [[x1, y, z1], [x2, y, z1], [x2, y, z2], [x1, y, z2]];
const label = (p, text, cls = '', dx = 0, dy = 0) => {
  const [x, y] = P(...p), w = text.length * 6.5 + 16;
  return `<g class="lbl ${cls}" transform="translate(${x + dx},${y + dy})"><rect x="${-w / 2}" y="-11" width="${w}" height="22" rx="5"/><text text-anchor="middle" y="4">${text}</text></g>`;
};

function opening(x1, x2, z1, z2, id, withWindow) {
  let s = poly(rectY(x1, x2, z1, z2, 4), 'face hole');
  s += poly([[x1, 4, z1], [x2, 4, z1], [x2, 3.8, z1], [x1, 3.8, z1]], 'face top');
  s += poly([[x1, 4, z1], [x1, 3.8, z1], [x1, 3.8, z2], [x1, 4, z2]], 'face right');
  if (withWindow) {
    const t = 0.07, mid = (x1 + x2) / 2;
    const ring = `M${pts(rectY(x1, x2, z1, z2, 3.9))}Z M${pts(rectY(x1 + t, x2 - t, z1 + t, z2 - t, 3.9))}Z`;
    s += `<clipPath id="${id}">${poly(rectY(x1, x2, z1, z2, 4), '')}</clipPath><g clip-path="url(#${id})">` +
      poly(rectY(x1 + t, x2 - t, z1 + t, z2 - t, 3.9), 'glass') +
      `<path d="${ring}" class="frame"/>` +
      seg([mid, 3.9, z1 + t], [mid, 3.9, z2 - t], 'thin') + '</g>';
  }
  return s;
}

function box3(x1, x2, y1, y2, z1, z2, cls) {
  const c = (x, y, z) => [x, y, z], e = [];
  for (const z of [z1, z2]) e.push([c(x1, y1, z), c(x2, y1, z)], [c(x2, y1, z), c(x2, y2, z)], [c(x2, y2, z), c(x1, y2, z)], [c(x1, y2, z), c(x1, y1, z)]);
  for (const [x, y] of [[x1, y1], [x2, y1], [x2, y2], [x1, y2]]) e.push([c(x, y, z1), c(x, y, z2)]);
  return e.map(([a, b]) => seg(a, b, cls)).join('');
}

function scene(o = {}) {
  let s = `<defs>
    <pattern id="hAdd" width="7" height="7" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="7" height="7" fill="#dff0d8"/><line x1="0" y1="0" x2="0" y2="7" stroke="#3d7f3a" stroke-width="2.4"/></pattern>
    <pattern id="hDel" width="7" height="7" patternUnits="userSpaceOnUse" patternTransform="rotate(-45)"><rect width="7" height="7" fill="#f8ddd7"/><line x1="0" y1="0" x2="0" y2="7" stroke="#ae3a28" stroke-width="2.4"/></pattern>
  </defs>`;
  if (!o.render) {
    for (let i = -3; i <= 10; i++) s += seg([i, -3, 0], [i, 8, 0], 'grid');
    for (let j = -3; j <= 8; j++) s += seg([-3, j, 0], [10, j, 0], 'grid');
    s += seg([0, 0, 0], [10, 0, 0], 'ax x') + seg([0, 0, 0], [0, 8, 0], 'ax y') + seg([0, 0, 0], [0, 0, 4.4], 'ax z');
  } else {
    s += poly([[0, 4, 0], [6, 4, 0], [4.4, 5.9, 0], [-1.6, 5.9, 0], [-1.6, 1.9, 0], [0, 0, 0]], '', 'fill="rgba(60,55,45,.28)"');
  }
  let room = poly([[.2, .2, 0], [5.8, .2, 0], [5.8, 3.8, 0], [.2, 3.8, 0]], 'face floor') +
    poly([[.2, .2, 0], [.2, 3.8, 0], [.2, 3.8, H], [.2, .2, H]], 'face inr') +
    poly([[.2, .2, 0], [5.8, .2, 0], [5.8, .2, H], [.2, .2, H]], 'face inl') +
    poly(rectY(0, 6, 0, H, 4), 'face left') +
    poly([[6, 0, 0], [6, 4, 0], [6, 4, H], [6, 0, H]], 'face right') +
    `<path class="face top" fill-rule="evenodd" d="M${pts([[0, 0, H], [6, 0, H], [6, 4, H], [0, 4, H]])}Z M${pts([[.2, .2, H], [5.8, .2, H], [5.8, 3.8, H], [.2, 3.8, H]])}Z"/>` +
    opening(1.0, 2.2, 0.9, 1.9, 'w1', !o.drawing);
  const w2 = o.drawing ? '' : opening(3.8, 5.0, 0.9, 1.9, 'w2', true);
  s += o.dim ? `${room}<rect x="-3000" y="-3000" width="6000" height="6000" class="wash"/>${w2}` : room + w2;

  if (o.drawing) {
    s += seg([-0.6, 4, 0.9], [6.6, 4, 0.9], 'guide') + label([-0.6, 4, 0.9], '0.90 m', '', -34, 0);
    s += poly(rectY(3.8, 5.0, 0.9, 1.9, 4), 'rubber');
    s += label([4.4, 4, 0.9], '1.20 m', '', 0, 16) + label([3.8, 4, 1.4], '1.00 m', '', -36, 0);
    const [cx, cy] = P(5.0, 4, 1.9);
    s += `<rect class="marker" x="${cx - 5}" y="${cy - 5}" width="10" height="10" transform="rotate(45 ${cx} ${cy})"/>`;
    s += label([5.0, 4, 1.9], 'On face', 'inf', 46, -22);
    s += `<path class="cursor" transform="translate(${cx + 5},${cy + 5})" d="M0 0l0 17 4.6-4.2 3 6.8 2.8-1.3-3-6.6 6.2-.4z"/>`;
  }
  if (o.select) s += box3(3.8, 5.0, 3.8, 4, 0.9, 1.9, 'selbox');
  if (o.dim) s += box3(3.72, 5.08, 3.74, 4.06, 0.82, 1.98, 'ctxbox');
  if (o.preview) {
    s += poly(rectY(3.7, 3.8, 0.9, 1.9, 4), '', 'fill="url(#hDel)" stroke="#ae3a28"');
    s += poly(rectY(5.0, 5.1, 0.9, 1.9, 4), '', 'fill="url(#hDel)" stroke="#ae3a28"');
    const t = 0.07, ring = `M${pts(rectY(3.7, 5.1, 0.9, 1.9, 4))}Z M${pts(rectY(3.7 + t, 5.1 - t, 0.9 + t, 1.9 - t, 4))}Z`;
    s += `<path d="${ring}" fill="url(#hAdd)" fill-rule="evenodd" stroke="#3d7f3a"/>`;
    s += poly(rectY(3.7, 5.1, 0.9, 1.9, 4), 'mod');
    s += seg([3.7, 4, 2.12], [5.1, 4, 2.12], 'dimline') + seg([3.7, 4, 2.04], [3.7, 4, 2.2], 'dimline') + seg([5.1, 4, 2.04], [5.1, 4, 2.2], 'dimline');
    s += label([4.4, 4, 2.12], '1.20 m to 1.40 m', 'amber', 0, -16);
  }
  return `<svg class="scene${o.render ? ' render' : ''}" viewBox="${o.render ? '-330 -230 800 620' : '-365 -250 800 640'}" preserveAspectRatio="xMidYMid meet">${s}</svg>`;
}

// --- Chrome ---
const ICONS = {
  select: 'M6 3l12 9-5.5 1L10 19z', line: 'M5 19L19 5M4 20h2v-2H4zM18 6h2V4h-2z', rect: 'M5 7h14v10H5z',
  circle: 'M12 5a7 7 0 1 0 0 14 7 7 0 0 0 0-14z', arc: 'M4 17A12 12 0 0 1 20 9',
  pushpull: 'M5 15l7 3.5 7-3.5M5 15l7-3.5 7 3.5M12 11.5V3m-3 3l3-3 3 3',
  move: 'M12 3v18M3 12h18M9.5 5.5L12 3l2.5 2.5M9.5 18.5L12 21l2.5-2.5M5.5 9.5L3 12l2.5 2.5M18.5 9.5L21 12l-2.5 2.5',
  rotate: 'M19 12a7 7 0 1 1-2.5-5.4M17 3v4h-4', scale: 'M4 20h7v-7H4zM11 13l8-8m0 0h-5m5 0v5',
  offset: 'M4 4h16v16H4zM8 8h8v8H8z', tape: 'M4 16L16 4l4 4L8 20zM9 11l2 2M12 8l2 2',
  eraser: 'M9 20h10M5 15l8-9 6 5-7 9H9z', paint: 'M5 11l7-7 7 7-7 7zM3 20h6',
  orbit: 'M12 4a8 8 0 1 0 0 16 8 8 0 0 0 0-16zM3.5 12c0 2 3.8 3.5 8.5 3.5s8.5-1.5 8.5-3.5',
  pan: 'M12 3v18M3 12h18', zoom: 'M10.5 4a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13zM15.5 15.5L20 20',
  info: 'M12 4a8 8 0 1 0 0 16 8 8 0 0 0 0-16zM12 11v5M12 8v.5', tree: 'M5 5h5M5 5v13h6M5 11h6M14 5h5M14 11h5M14 18h5',
  swatch: 'M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z', tag: 'M4 4h8l8 8-8 8-8-8zM8.5 8.5v.5',
  clock: 'M12 4a8 8 0 1 0 0 16 8 8 0 0 0 0-16zM12 8v4l3 2', spark: 'M12 3l2 6 6 2-6 2-2 6-2-6-6-2 6-2z', menu: 'M4 7h16M4 12h16M4 17h16',
};
const ico = n => `<svg class="ico" viewBox="0 0 24 24"><path d="${ICONS[n]}"/></svg>`;
const RAIL = ['select', '-', 'line', 'rect', 'circle', 'arc', '-', 'pushpull', 'move', 'rotate', 'scale', 'offset', '-', 'tape', 'eraser', 'paint', '-', 'orbit', 'pan', 'zoom'];
const rail = on => `<nav class="rail">${RAIL.map(t => t === '-' ? '<hr>' : `<span class="tool${t === on ? ' on' : ''}">${ico(t)}</span>`).join('')}</nav>`;

const top = (state, narrow) => `<div class="top">${narrow ? `<span class="menu">${ico('menu')}</span>` :
  ['File', 'Edit', 'View', 'Camera', 'Draw', 'Tools', 'Window', 'Help'].map(m => `<span class="menu">${m}</span>`).join('')}
  <div class="doc${state === 'Not saved' ? ' failed' : ''}"><span>room</span>${state === 'Saved' ? '' : '<span class="dot"></span>'}<span class="state">${state}</span></div>
  <div class="palette">${narrow ? 'Search' : 'Search commands'} <kbd>Ctrl K</kbd></div></div>`;

const status = (tool, hint, mlabel, mval, job) => `<div class="status"><span class="tool">${tool}</span><span class="vr"></span>
  <span class="hint">${hint}</span>${job ? `<span class="jobchip">Render 2 <i></i> 42%</span>` : ''}
  <span class="meas">${mlabel}<span>${mval || '&nbsp;'}</span></span></div>`;

const views = `<div class="views"><span class="on">3D</span><span>Top</span><span>Front</span><span>Side</span><span>Fit</span></div>`;
const crumb = parts => `<div class="crumb">${parts.map((p, i) => i === parts.length - 1 ? `<b>${p}</b>` : `<span>${p}</span><span class="sep">›</span>`).join('')}</div>`;

const row = (depth, name, kind, o = {}) => `<div class="row${o.sel ? ' sel' : ''}" style="padding-left:${12 + depth * 14}px">
  <span class="tw">${o.open ? '▾' : o.leaf ? '' : '▸'}</span><span class="box${o.comp ? ' comp' : ''}"></span>${name}<span class="kind">${kind}</span></div>`;
const outliner = (sel, windows = true) => `<section class="sec"><h3>Outliner<span class="n">${windows ? 5 : 3} items</span></h3>
  ${row(0, 'Room', 'Group', { open: true })}${row(1, 'Walls', 'Group')}${row(1, 'Floor', 'Group', { leaf: true })}
  ${windows ? row(1, 'Window #1', 'Component', { comp: true }) + row(1, 'Window #2', 'Component', { comp: true, sel }) : ''}<div style="height:8px"></div></section>`;
const materials = `<section class="sec"><h3>Materials<span class="n">Chalk</span></h3><div class="body"><div class="swatches">
  <i class="on" style="background:#f4f3ee"></i><i style="background:#c9b08a"></i><i style="background:#8a8d88"></i><i style="background:#a9c6d6"></i><i style="background:#7c5a44"></i><i style="background:#3f4a45"></i></div></div></section>`;
const shut = n => `<section class="sec shut"><h3>${n}</h3></section>`;
const tabs = on => `<div class="tabs"><span class="${on === 'Tray' ? 'on' : ''}">Tray</span><span class="${on === 'Assistant' ? 'on' : ''}">Assistant</span></div>`;

const entityEmpty = `<section class="sec"><h3>Entity info</h3><div class="body"><div class="empty">Nothing selected. Select something to see its measurements.</div></div></section>`;
const entityWindow = defn => `<section class="sec"><h3>Entity info<span class="n">Component</span></h3><div class="body">
  <div class="field"><label>Name</label><span class="in">Window #2</span></div>
  <div class="field"><label>Definition</label><span class="in">Window</span></div>
  <div class="field"><label>Tag</label><span class="in">Openings</span></div>
  <div class="field"><label>Material</label><span class="in">Chalk</span></div>
  <div class="trio"><span class="in"><i style="color:var(--x)">W</i>1.20 m</span><span class="in"><i style="color:var(--y)">D</i>0.10 m</span><span class="in"><i style="color:var(--z)">H</i>1.00 m</span></div>
  <div class="note">${defn ? 'Editing the definition. Used by 2 instances.' : 'Used 2 times in this model.'}</div></div></section>`;
const history = (items) => `<section class="sec"><h3>History</h3>${items.map((h, i) => `<div class="hist${i === 0 ? ' now' : ''}">${h}</div>`).join('')}<div style="height:8px"></div></section>`;
const side = (tab, inner) => `<aside class="side">${tabs(tab)}${inner}</aside>`;

const assistant = `<div class="asst">
  <div class="provider"><span class="live"></span><b>Remote</b> provider model<span class="sent">What is sent</span></div>
  <div class="chat">
    <div class="msg you">Make the selected window 20 cm wider, keeping it centered.</div>
    <div class="steps">▸ Inspected the selection in 6 steps<b>The clear opening is 1.20 m wide. The definition is shared by Window #1.</b></div>
    <div class="card"><header><span>Proposed change</span><span>Revision 42</span></header>
      <h4>Widen Window #2 by 0.20 m</h4>
      <div class="kv"><span>Clear width</span><span>1.20 m to 1.40 m</span></div>
      <div class="kv"><span>Center, sill, height</span><span class="keep">Unchanged ✓</span></div>
      <div class="kv"><span>Window #1</span><span class="keep">Unchanged ✓</span></div>
      <div class="kv"><span>Window #2</span><span>Made unique</span></div>
      <div class="counts">4 faces modified, none created or deleted</div>
      <footer><span class="btn primary">Apply</span><span class="btn">Discard</span><span class="btn">Refine</span></footer>
    </div>
  </div>
  <div class="composer"><span class="chip">Window #2 in Room <span class="x">✕</span></span>
    <div class="box-in">Ask or describe a change</div>
    <div class="foot"><span>Preview first ▾</span><span><kbd>Ctrl J</kbd></span></div></div></div>`;

// --- Screens ---
const hint = (text, keys) => text + keys.map(([k, t]) => ` &nbsp;<kbd>${k}</kbd> ${t}`).join('');
const SCREENS = {
  draw: () => top('Edited') + `<main>${rail('rect')}<section class="viewport">${scene({ drawing: true })}${crumb(['Model', 'Room', 'Walls'])}${views}</section>` +
    side('Tray', entityEmpty + outliner(false, false) + materials + shut('Tags') +
      history(['Push/pull opening 0.20 m', 'Rectangle 1.20 m, 1.00 m', 'Push/pull walls 2.70 m', 'Offset 0.20 m', 'Rectangle 6.00 m, 4.00 m'])) + `</main>` +
    status('Rectangle', hint('Click the opposite corner, or type the size.', [['Shift', 'locks to this face'], ['Esc', 'cancels']]), 'Dimensions', '1.20 m, 1.00 m'),

  context: () => top('Edited') + `<main>${rail('select')}<section class="viewport">${scene({ dim: true })}${crumb(['Model', 'Room', 'Window #2'])}${views}
    <div class="banner">Editing a definition used by 2 instances. Changes apply to both.<span class="btn primary">Make unique</span><span class="btn">Close <kbd>Esc</kbd></span></div></section>` +
    side('Tray', entityWindow(true) + outliner(true) + materials + shut('Tags') + shut('History')) + `</main>` +
    status('Select', hint('Click to select inside Window #2. Double-click outside to leave.', [['Esc', 'closes one level']]), 'Measurements', ''),

  assistant: () => top('Edited') + `<main>${rail('select')}<section class="viewport">${scene({ preview: true })}${crumb(['Model', 'Room'])}${views}
    <div class="banner warn"><b>Preview.</b> Not in your model yet.<span class="btn primary">Apply <kbd>Ctrl Enter</kbd></span><span class="btn">Discard</span></div></section>` +
    side('Assistant', assistant) + `</main>` +
    status('Select', 'Previewing an assistant change. You can orbit and measure it before applying.', 'Measurements', ''),

  render: () => top('Saved') + `<main>${rail('select')}<section class="viewport">
    <div class="vtabs"><span>Model</span><span class="on">Render 1</span></div>
    <div class="render-view"><div class="img">${scene({ render: true })}</div></div>
    <div class="render-actions"><span class="btn">Open scene in Blender</span><span class="btn">Render again</span><span class="btn primary">Save image as…</span></div>
    <div class="render-bar"><span class="prov">From revision 42. <b>The model has changed since.</b> Blender 5.0, Final preset, GPU, 1 min 48 s</span></div>
    <div class="jobs"><h4>Jobs</h4>
      <div class="job"><span>Render 2<br><small>Final preset, revision 45, 0 min 52 s</small></span><span class="btn">Cancel</span><div class="bar"><i></i></div></div>
      <div class="job"><span>Render 1<br><small>Finished at 14:02</small></span><span class="btn">Show</span></div></div></section>` +
    side('Tray', entityEmpty + outliner(false) + materials + shut('Tags') + shut('History')) + `</main>` +
    status('Select', 'Click an object to select it. Rendering continues while you model.', 'Measurements', '', true),

  narrow: () => top('Not saved', true) + `<main>${rail('select')}<section class="viewport">${scene({ select: true })}${crumb(['Model', 'Room'])}
    <div class="banner fail" style="top:52px"><span><b>Not saved.</b> The disk is full. Your edits are safe in recovery data.</span><span class="btn primary">Retry</span><span class="btn">Save as…</span></div></section>
    <aside class="strip">${['info', 'tree', 'swatch', 'tag', 'clock', 'spark'].map(i => `<span>${ico(i)}</span>`).join('')}</aside></main>` +
    status('Select', 'Window #2 selected.', 'Measurements', ''),
};

const name = new URLSearchParams(location.search).get('screen') || 'draw';
if (name === 'render') document.documentElement.classList.add('dark');
document.body.innerHTML = SCREENS[name]();
})();
