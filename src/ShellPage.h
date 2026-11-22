#pragma once
#include "Logo.h"
// Kadran's front page, served at /: the devices of the network and their namespaces on the left, the controls page (pxs, ControlsPage.h) in a
// frame on the right. An address is dragged from the tree onto the page (HTML5 drag and drop, the page takes the drop and tells the application
// where), or double-clicked, or put on the page from its menu (which widget; two numbers: an XY pad or two controls).
// One file, no CDN: the script and the style are what the Content-Security-Policy lets run (a hash of each). The scripts stay under 16 KB.
#include <QByteArray>

namespace shell_page {

inline constexpr char kStyle[] = R"CSS(
:root { color-scheme: dark; --bg:#121215; --panel:#1c1c21; --panel2:#25252b; --line:#31313a; --text:#ececf0; --dim:#8a8a94; --acc:#29c7e0; --ok:#46c46a; --bad:#e5564a; }
* { box-sizing:border-box; }
[hidden] { display:none !important; }
html, body { height:100%; margin:0; background:var(--bg); color:var(--text); font:13px/1.3 -apple-system, system-ui, "Segoe UI", sans-serif; overflow:hidden; }
#app { display:flex; height:100%; }
#side { width:340px; min-width:200px; max-width:70vw; display:flex; flex-direction:column; border-right:1px solid var(--line); background:var(--panel); }
#grip { width:5px; margin-left:-3px; cursor:col-resize; z-index:3; }
#grip:hover, #grip.on { background:var(--acc); opacity:.5; }
#main { flex:1; min-width:0; position:relative; }
#frame { position:absolute; inset:0; width:100%; height:100%; border:0; background:var(--bg); }
body.resizing #frame { pointer-events:none; }
body.nosb #side, body.nosb #grip { display:none; }
#float { position:absolute; left:8px; top:8px; z-index:5; display:flex; gap:6px; opacity:.35; transition:opacity .15s; }
#float:hover { opacity:1; }
#float button { width:30px; height:30px; padding:0; border-radius:8px; background:var(--panel2); border:1px solid var(--line); color:var(--text); font-size:15px; cursor:pointer; }
body:not(.nosb) #float { left:auto; right:8px; top:auto; bottom:8px; }
.bar { display:flex; flex-wrap:wrap; align-items:center; padding:8px 10px; border-bottom:1px solid var(--line); gap:6px; }
.bar h1 { flex:1; margin:0; font-size:15px; font-weight:600; letter-spacing:.02em; }
.bar .dot { width:8px; height:8px; border-radius:50%; background:var(--bad); }
.bar .dot.ok { background:var(--ok); }
button, .btn { font:inherit; color:var(--text); background:var(--panel2); border:1px solid var(--line); border-radius:7px; padding:5px 10px; cursor:pointer; }
button:hover { border-color:var(--acc); }
button.on { background:var(--acc); border-color:var(--acc); color:#06222a; font-weight:600; }
#views { display:flex; flex-wrap:wrap; align-items:center; gap:5px; padding:6px 10px; border-bottom:1px solid var(--line); }
#views button { font:inherit; font-size:12px; color:var(--text); background:var(--bg); border:1px solid var(--line); border-radius:12px; padding:3px 10px; }
#views button.on { background:var(--acc); border-color:var(--acc); color:#06222a; font-weight:600; }
#views .none { color:var(--dim); font-size:12px; }
#find { margin:8px 10px; padding:6px 9px; font:inherit; color:var(--text); background:var(--bg); border:1px solid var(--line); border-radius:7px; outline:none; }
#find:focus { border-color:var(--acc); }
#tree { flex:1; overflow:auto; padding:0 0 12px; -webkit-user-select:none; user-select:none; }
.row { display:flex; align-items:center; height:24px; padding-right:8px; white-space:nowrap; cursor:default; }
.row:hover { background:var(--panel2); }
.row.sel { background:#1d3a42; }
.row .arrow { width:16px; text-align:center; color:var(--dim); font-size:10px; flex:none; }
.row .name { overflow:hidden; text-overflow:ellipsis; flex:1; }
.row .ty { color:var(--dim); font-size:11px; margin-left:8px; font-family:ui-monospace, Menlo, monospace; }
.row.dev { height:28px; font-weight:600; }
.row.dev .st { width:7px; height:7px; border-radius:50%; margin-right:7px; background:var(--dim); flex:none; }
.row.dev .st.ready { background:var(--ok); } .row.dev .st.failed { background:var(--bad); } .row.dev .st.connecting { background:#e5c14a; }
.row.dead .name { color:var(--dim); }
.row .act { margin-left:6px; padding:0 4px; color:var(--dim); cursor:pointer; flex:none; visibility:hidden; }
.row:hover .act { visibility:visible; } .row .act:hover { color:var(--text); }
.row.add .name { color:var(--acc); }
#dlg select, #dlg .two { margin:8px 0 0; } #dlg .two { display:flex; gap:8px; } #dlg .two input { margin-bottom:12px; }
#dlg .near { max-height:150px; overflow:auto; margin:4px 0 10px; border:1px solid var(--line); border-radius:7px; }
#dlg .near div { padding:5px 8px; cursor:pointer; } #dlg .near div:hover { background:var(--panel2); } #dlg .near small { color:var(--dim); }
.row.ro .name { color:var(--dim); }
.note { color:var(--dim); padding:10px 14px; line-height:1.5; }
#status { padding:6px 10px; border-top:1px solid var(--line); color:var(--dim); min-height:28px; font-size:12px; }
#menu { position:fixed; z-index:20; min-width:190px; background:var(--panel2); border:1px solid var(--line); border-radius:9px; padding:4px; box-shadow:0 8px 24px #0009; }
#menu div { padding:6px 12px; border-radius:6px; cursor:default; white-space:nowrap; }
#menu div:hover { background:var(--acc); color:#06222a; }
#menu .sep { height:1px; margin:4px 0; padding:0; background:var(--line); }
#menu .head { color:var(--dim); font-size:11px; padding:4px 12px; }
#menu .head:hover { background:none; color:var(--dim); }
#dlg { position:fixed; inset:0; z-index:30; background:#0009; display:flex; align-items:center; justify-content:center; }
#dlg form { background:var(--panel); border:1px solid var(--line); border-radius:12px; padding:18px; width:320px; }
#dlg input { width:100%; margin:8px 0 12px; padding:8px; font:inherit; color:var(--text); background:var(--bg); border:1px solid var(--line); border-radius:7px; }
#dlg .acts { text-align:right; }
)CSS";

inline constexpr char kBody[] = R"HTML(
<div id="app">
  <div id="side">
    <div class="bar"><span class="dot" id="dot"></span><h1>Kadran</h1><button id="add" title="Add a device: OSCQuery, or plain OSC by its address and UDP port">+ Device</button></div>
    <div class="bar"><button id="open">Open…</button><button id="save">Save</button><button id="undo" title="Undo (Cmd/Ctrl+Z)">↶</button><button id="redo" title="Redo (Shift+Cmd/Ctrl+Z)">↷</button><span style="flex:1"></span><button id="prefs" title="Preferences: the widgets and sizes of the new controls">Prefs</button><button id="about" title="About Kadran">About</button><button id="pages" title="Pages of the interface">Pages ▾</button><button id="edit" title="Edit the page (Cmd/Ctrl+E)">Edit</button></div>
    <div id="views"></div>
    <input id="find" type="search" placeholder="Filter the addresses" autocomplete="off">
    <div id="tree"></div>
    <div id="status"></div>
  </div>
  <div id="grip"></div>
  <div id="main"><iframe id="frame" src="/control" title="Controls"></iframe><div id="float"><button id="sbtoggle" title="Hide / show the side panel (Cmd/Ctrl+K)">☰</button><button id="fs" title="Full screen">⛶</button></div></div>
</div>
<input id="file" type="file" accept=".kadran,application/json" hidden>
<div id="menu" hidden></div>
)HTML";

inline constexpr char kScript[] = R"JS(
(() => {
  const $ = s => document.querySelector(s);
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
  const wName = { fader: 'Fader', vfader: 'Vertical fader', slim: 'Slim fader', vslim: 'Slim vertical fader', knob: 'Encoder', number: 'Number', switch: 'Switch', buttons: 'Buttons', menu: 'Menu', hold: 'Hold button', tap: 'Tap button', meter: 'Meter', hmeter: 'Horizontal meter', readout: 'Readout', graph: 'Graph', led: 'LED' };
  let ws = null, devices = [], nearby = [], trees = {}, openNodes = new Set(), selected = new Map(), filter = '', lastRow = null, statusTimer = 0;
  const send = o => { if (ws && ws.readyState === 1) ws.send(JSON.stringify(o)); };
  const say = t => { $('#status').textContent = t || ''; clearTimeout(statusTimer); if (t) statusTimer = setTimeout(() => { $('#status').textContent = ''; }, 6000); };

  function connect() {
    ws = new WebSocket('ws://' + location.host + '/ws');
    ws.onopen = () => { $('#dot').classList.add('ok'); send({ t: 'hello' }); };
    ws.onclose = () => { $('#dot').classList.remove('ok'); setTimeout(connect, 1500); };
    ws.onmessage = e => {
      let m; try { m = JSON.parse(e.data); } catch (_) { return; }
      if (m.t === 'devices') { devices = m.list || []; nearby = m.nearby || []; render(); }
      else if (m.t === 'tree') { trees[m.device] = m.root || null; render(); }
      else if (m.t === 'about') aboutHtml = m.html;
      else if (m.t === 'prefs') { prefs = m.prefs; prefOpts = m.options; }
      else if (m.t === 'views') { views = m.list || []; activeView = m.active || ''; renderViews(); }
      else if (m.t === 'say') say(m.text);
      else if (m.t === 'confirm') { if (confirm('Add ' + m.n + ' controls, as a ' + m.as + '?')) send({ t: 'addNode', device: m.device, path: m.path, as: m.as, page: m.page, confirmed: true }); }
      else if (m.t === 'file') download(m.name || 'Interface.kadran', JSON.stringify(m.doc, null, 2));
    };
  }
  function download(name, text) {
    const a = el('a'); a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' })); a.download = name;
    document.body.appendChild(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 500);
  }

  // The tree. A row is a device or a node; the nodes open lazily, and what matches the filter shows with the nodes above it
  const key = (d, p) => d + ':' + p;
  const matches = (n, f) => (n.p || '').toLowerCase().indexOf(f) >= 0;
  const hasMatch = (n, f) => matches(n, f) || (n.c || []).some(c => hasMatch(c, f));
  function render() {
    const tree = $('#tree'), keep = tree.scrollTop; tree.textContent = '';
    if (!devices.length) tree.appendChild(el('div', 'note', 'Looking for OSCQuery devices on the network… Add one by its address with + Device (a plain OSC device too).'));
    devices.forEach(d => {
      const row = el('div', 'row dev' + (d.online ? '' : ' dead'));
      const k = key(d.id, '');
      const opened = openNodes.has(k);
      row.appendChild(el('span', 'arrow', opened ? '▼' : '▶'));
      row.appendChild(el('span', 'st ' + d.state));
      row.appendChild(el('span', 'name', d.name));
      const rn = el('span', 'act', '✎'); rn.title = 'Rename'; rn.onclick = e => { e.stopPropagation(); const v = prompt('Name of this device', d.name); if (v && v.trim()) send({ t: 'renameDevice', device: d.id, name: v.trim() }); };
      const rm = el('span', 'act', '✕'); rm.title = 'Remove this device'; rm.onclick = e => { e.stopPropagation(); send({ t: 'removeDevice', device: d.id }); };
      row.appendChild(rn); row.appendChild(rm);
      row.title = (d.kind === 'osc' ? 'OSC (UDP) ' : 'OSCQuery ') + d.address + (d.state === 'failed' ? ' (not answering)' : '');
      row.onclick = () => { if (openNodes.has(k)) openNodes.delete(k); else { openNodes.add(k); send({ t: 'open', device: d.id }); } render(); };
      tree.appendChild(row);
      if (opened && d.kind === 'osc') {
        const root = trees[d.id];
        (root && root.c || []).forEach(n => nodeRows(tree, d.id, n, 1, true));
        const add = el('div', 'row add'); add.style.paddingLeft = '24px'; add.appendChild(el('span', 'name', '+ Address…'));
        add.title = 'Declare an address this device takes'; add.onclick = () => pointDialog(d); tree.appendChild(add);
      } else if (opened) {
        const root = trees[d.id];
        if (!root) tree.appendChild(el('div', 'note', d.state === 'failed' ? 'Not answering. Trying again…' : 'Reading its namespace…'));
        else (root.c || []).forEach(n => nodeRows(tree, d.id, n, 1));
      }
    });
    tree.scrollTop = keep;
  }
  function nodeRows(into, dev, n, depth, mine) {
    if (filter && !hasMatch(n, filter)) return;
    const k = key(dev, n.p), leaf = !n.c, can = !!n.w;
    const open = filter ? true : openNodes.has(k);
    const row = el('div', 'row' + (leaf ? '' : '') + (can ? '' : ' ro') + (selected.has(k) ? ' sel' : ''));
    row.style.paddingLeft = (10 + depth * 14) + 'px';
    row.appendChild(el('span', 'arrow', leaf ? '' : (open ? '▼' : '▶')));
    row.appendChild(el('span', 'name', n.n));
    if (n.t) row.appendChild(el('span', 'ty', n.t));
    if (mine && leaf) { const x = el('span', 'act', '✕'); x.title = 'Remove this address'; x.onclick = e => { e.stopPropagation(); send({ t: 'removePoint', device: dev, path: n.p }); }; row.appendChild(x); }
    row.title = n.p + (n.d ? '\n' + n.d : '') + (leaf && !can ? '\n(more than one argument, or text: not a control yet)' : '');
    if (!leaf) { // a node: dragged, it brings all it holds, in a group of the page (Alt: in a page of its own)
      row.draggable = true;
      row.ondragstart = e => {
        const as = e.altKey ? 'page' : 'ask'; // the page asks: a group of the page, or a page of its own
        e.dataTransfer.setData('application/x-pxs-address', JSON.stringify([{ device: dev, path: n.p, node: true, as }]));
        e.dataTransfer.setData('text/plain', n.p);
        e.dataTransfer.effectAllowed = 'copy';
      };
    }
    if (can) {
      row.draggable = true;
      row.ondragstart = e => {
        if (!selected.has(k)) { selected.clear(); selected.set(k, { device: dev, path: n.p, w: n.w }); }
        const list = Array.from(selected.values()).map(s => ({ device: s.device, path: s.path }));
        e.dataTransfer.setData('application/x-pxs-address', JSON.stringify(list));
        e.dataTransfer.setData('text/plain', list.map(s => s.path).join('\n'));
        e.dataTransfer.effectAllowed = 'copy';
      };
      row.ondblclick = () => add([{ device: dev, path: n.p }], '', true);
    }
    row.onclick = e => {
      if (!leaf && !e.metaKey && !e.ctrlKey && !e.shiftKey) { if (openNodes.has(k)) openNodes.delete(k); else openNodes.add(k); render(); return; }
      if (!can) return;
      if (e.metaKey || e.ctrlKey) { if (selected.has(k)) selected.delete(k); else selected.set(k, { device: dev, path: n.p, w: n.w }); }
      else if (e.shiftKey && lastRow && lastRow.dev === dev) rangeTo(dev, n.p);
      else { selected.clear(); selected.set(k, { device: dev, path: n.p, w: n.w }); }
      lastRow = { dev, path: n.p }; render();
    };
    row.oncontextmenu = e => {
      e.preventDefault();
      if (!leaf) return menuFor(e, null, n, dev);
      if (can && !selected.has(k)) { selected.clear(); selected.set(k, { device: dev, path: n.p, w: n.w }); render(); }
      menuFor(e, Array.from(selected.values()), n, dev);
    };
    into.appendChild(row);
    if (!leaf && open) n.c.forEach(c => nodeRows(into, dev, c, depth + 1, mine));
  }
)JS";

inline constexpr char kScriptMid[] = R"JS(
  function rangeTo(dev, path) { // shift-click: the addresses between the last one picked and this one, as the tree shows them
    const rows = Array.from(document.querySelectorAll('#tree .row')), names = [];
    const all = []; const walk = (d, n) => { if (n.w) all.push({ device: d, path: n.p, w: n.w }); (n.c || []).forEach(c => walk(d, c)); };
    devices.forEach(d => { if (trees[d.id]) walk(d.id, trees[d.id]); });
    const a = all.findIndex(s => s.device === lastRow.dev && s.path === lastRow.path), b = all.findIndex(s => s.device === dev && s.path === path);
    if (a < 0 || b < 0) return;
    selected.clear(); all.slice(Math.min(a, b), Math.max(a, b) + 1).forEach(s => selected.set(key(s.device, s.path), s));
  }

  // Putting addresses on the page
  const frame = $('#frame');
  const pageId = () => { try { return String(frame.contentWindow.pxsPageId()); } catch (_) { return '-1'; } };
  function add(targets, widget, pad, macro) { send({ t: 'add', targets, widget: widget || '', pad: !!pad, macro: macro || '', page: pageId() }); }
  function addNode(device, path, as) { send({ t: 'addNode', device, path, as, page: pageId() }); }
  function menuFor(e, list, n, dev) {
    const m = $('#menu'); m.textContent = ''; m.hidden = false;
    const item = (text, fn, cls) => { const d = el('div', cls || '', text); if (fn) d.onclick = () => { closeMenu(); fn(); }; m.appendChild(d); };
    const targets = list ? list.map(s => ({ device: s.device, path: s.path })) : [];
    if (!list) {
      item('Add all its parameters as a group of the page', () => addNode(dev, n.p, 'group'));
      item('Add all its parameters as a new page', () => addNode(dev, n.p, 'page'));
    } else if (list.length === 1) {
      item('Add to the page', () => add(targets, '', true));
      item('Add as', null, 'head');
      list[0].w.forEach(w => item('   ' + (wName[w] || w), () => add(targets, w, false)));
    } else {
      const numeric = list.every(s => s.w[0] === 'vfader'), same = list.every(s => s.device === list[0].device);
      if (list.length === 2 && numeric && same) { item('Add as an XY pad', () => add(targets, '', true)); item('Add as two', null, 'head'); list[0].w.forEach(w => item('   ' + (wName[w] || w), () => add(targets, w, false))); }
      else { item('Add ' + list.length + ' controls', () => add(targets, '', false)); if (numeric && same) { item('Add all as', null, 'head'); list[0].w.forEach(w => item('   ' + (wName[w] || w), () => add(targets, w, false))); } }
      item('One control that sends to all of them', null, 'head');
      item('   A fader (each in its own range)', () => add(targets, '', false, 'slider'));
      item('   A switch', () => add(targets, '', false, 'switch'));
      item('   A scene button (once, when pressed)', () => add(targets, '', false, 'button'));
      item('   A hold button (high when pressed, low when let go)', () => add(targets, '', false, 'hold'));
    }
    m.style.left = Math.min(e.clientX, innerWidth - 220) + 'px'; m.style.top = Math.min(e.clientY, innerHeight - m.offsetHeight - 8) + 'px';
  }
  let aboutHtml = '';
  $('#about').onclick = () => {
    const dlg = el('div'); dlg.id = 'dlg'; const box = el('div'); box.style.cssText = 'max-width:480px;max-height:80vh;overflow:auto;background:var(--panel2);border:1px solid var(--line);border-radius:12px;padding:18px 20px';
    box.innerHTML = aboutHtml; box.querySelectorAll('a').forEach(a => { a.target = '_blank'; a.rel = 'noopener'; });
    const close = el('button', 'on', 'Close'); close.style.marginTop = '12px'; close.onclick = () => dlg.remove(); box.appendChild(close); dlg.appendChild(box); document.body.appendChild(dlg);
    dlg.onclick = e => { if (e.target === dlg) dlg.remove(); };
  };
  $('#pages').onclick = e => {
    e.stopPropagation();
    const m = $('#menu'); m.textContent = ''; m.hidden = false;
    const item = (text, fn, cls) => { const d = el('div', cls || '', text); if (fn) d.onclick = () => { closeMenu(); fn(); }; m.appendChild(d); };
    const ask = (op, text) => () => { if (!text || confirm(text)) send({ t: 'pages', op, page: pageId() }); };
    item('Add a page', ask('add'));
    item('Clear this page', ask('clear', 'Remove all the controls of this page?')); item('Clear all', ask('clearAll', 'Remove all the controls and pages?'));
    item('New empty interface', ask('new', 'Start a new, empty interface? (Undo brings the old one back.)'));
    const r = e.currentTarget.getBoundingClientRect(); m.style.left = Math.min(r.left, innerWidth - 220) + 'px'; m.style.top = (r.bottom + 4) + 'px';
  };
  const closeMenu = () => { $('#menu').hidden = true; };
  document.addEventListener('click', closeMenu); document.addEventListener('keydown', e => { if (e.key === 'Escape') closeMenu(); });
  window.addEventListener('blur', closeMenu);

  // The buttons, the keyboard, the width of the side panel
  $('#find').oninput = e => { filter = e.target.value.trim().toLowerCase(); render(); };
  $('#save').onclick = () => send({ t: 'save' });
  $('#undo').onclick = () => send({ t: 'undo' }); $('#redo').onclick = () => send({ t: 'redo' });
  document.addEventListener('keydown', e => {
    const k = (e.key || '').toLowerCase(), t = e.target && e.target.tagName;
    if (!(e.metaKey || e.ctrlKey) || e.altKey || t === 'INPUT' || t === 'TEXTAREA') return;
    if (k === 'z' || k === 'y') { e.preventDefault(); send({ t: (k === 'y' || e.shiftKey) ? 'redo' : 'undo' }); }
  });
  $('#open').onclick = () => $('#file').click();
  $('#file').onchange = e => { const f = e.target.files[0]; if (!f) return; const r = new FileReader(); r.onload = () => { try { send({ t: 'load', doc: JSON.parse(r.result) }); } catch (_) { say('Not an interface file'); } }; r.readAsText(f); e.target.value = ''; };
  // A dialog: its fields, and what Add does with them
  function dialog(title, build, submit) {
    const dlg = el('div'); dlg.id = 'dlg'; const form = el('form'); form.appendChild(el('div', '', title));
    const done = build(form);
    const acts = el('div', 'acts'), cancel = el('button', '', 'Cancel'), ok = el('button', 'on', 'Add'); cancel.type = 'button'; acts.appendChild(cancel); acts.appendChild(ok); form.appendChild(acts); dlg.appendChild(form); document.body.appendChild(dlg);
    cancel.onclick = () => dlg.remove(); dlg.onclick = e => { if (e.target === dlg) dlg.remove(); };
    form.onsubmit = e => { e.preventDefault(); if (submit(done) !== false) dlg.remove(); };
    const first = form.querySelector('input'); if (first) first.focus();
  }
  const field = (form, label, placeholder, value) => { const i = el('input'); i.placeholder = placeholder || ''; i.value = value || ''; i.title = label; form.appendChild(i); return i; };
  const hostPort = v => { v = v.trim(); const i = v.lastIndexOf(':'); return i > 0 && +v.slice(i + 1) > 0 && +v.slice(i + 1) < 65536 ? { host: v.slice(0, i), port: +v.slice(i + 1) } : null; };
  $('#add').onclick = () => dialog('Add a device', form => {
    const kind = el('select'); [['oscquery', 'OSCQuery (reads the namespace)'], ['osc', 'OSC (UDP: you give its addresses)']].forEach(o => { const op = el('option', '', o[1]); op.value = o[0]; kind.appendChild(op); });
    kind.style.cssText = 'width:100%;padding:8px;color:var(--text);background:var(--bg);border:1px solid var(--line);border-radius:7px;font:inherit'; form.appendChild(kind);
    const name = field(form, 'Name', 'Name (optional)'); const addr = field(form, 'Address', '192.168.1.20:5678');
    const near = el('div', 'near'); form.insertBefore(near, form.querySelector('.acts'));
    const sync = () => {
      const osc = kind.value === 'osc'; name.style.display = osc ? '' : 'none'; addr.placeholder = osc ? 'host or IP : UDP port, e.g. 192.168.1.20:9000' : 'host:port of the OSCQuery server, e.g. 192.168.1.20:5678';
      near.textContent = ''; near.style.display = osc && nearby.length ? '' : 'none';
      nearby.forEach(n => { const r = el('div'); r.appendChild(document.createTextNode(n.name + ' ')); r.appendChild(el('small', '', n.computer + ' · ' + n.address + ':' + n.port + (n.added ? ' · added' : ''))); r.onclick = () => { name.value = n.name; addr.value = n.address + ':' + n.port; }; near.appendChild(r); });
    };
    kind.onchange = sync; sync(); return { kind, name, addr };
  }, f => {
    const hp = hostPort(f.addr.value); if (!hp) { say('Give it as host:port'); return false; }
    send({ t: 'addDevice', kind: f.kind.value, name: f.name.value.trim(), host: hp.host, port: hp.port });
  });
  const pointDialog = d => dialog('An address ' + d.name + ' takes', form => {
    const path = field(form, 'Address', '/layer/1/opacity');
    const type = el('select'); [['f', 'Float'], ['i', 'Integer'], ['T', 'On / off'], ['N', 'Trigger (no value)']].forEach(o => { const op = el('option', '', o[1]); op.value = o[0]; type.appendChild(op); });
    type.style.cssText = 'width:100%;padding:8px;color:var(--text);background:var(--bg);border:1px solid var(--line);border-radius:7px;font:inherit'; form.appendChild(type);
    const two = el('div', 'two'); const min = field(two, 'Minimum', 'min 0'), max = field(two, 'Maximum', 'max 1'); form.appendChild(two);
    type.onchange = () => { two.style.display = type.value === 'f' || type.value === 'i' ? '' : 'none'; max.placeholder = type.value === 'i' ? 'max 127' : 'max 1'; }; return { path, type, min, max };
  }, f => {
    if (!f.path.value.trim().startsWith('/')) { say('An address starts with /'); return false; }
    send({ t: 'addPoint', device: d.id, path: f.path.value.trim(), type: f.type.value, min: f.min.value === '' ? 0 : +f.min.value, max: f.max.value === '' ? (f.type.value === 'i' ? 127 : 1) : +f.max.value });
  });
  const toggleEdit = () => { try { const b = frame.contentDocument.getElementById('edit'); if (b && !b.hidden) b.click(); $('#edit').classList.toggle('on', b && b.classList.contains('on')); } catch (_) {} };
  $('#edit').onclick = toggleEdit;
  document.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && !e.shiftKey && !e.altKey && (e.key === 'e' || e.key === 'E')) { e.preventDefault(); toggleEdit(); } });
  // The side panel hidden, and the whole window full screen: the controls alone
  const sidebar = on => { document.body.classList.toggle('nosb', !on); try { localStorage.setItem('kadran-side', on ? '1' : '0'); } catch (_) {} };
  try { if (localStorage.getItem('kadran-side') === '0') sidebar(false); } catch (_) {}
  $('#sbtoggle').onclick = () => sidebar(document.body.classList.contains('nosb'));
  const fullscreen = () => { const d = document, e = d.documentElement; if (d.fullscreenElement || d.webkitFullscreenElement) (d.exitFullscreen || d.webkitExitFullscreen).call(d); else if (e.requestFullscreen) e.requestFullscreen(); else if (e.webkitRequestFullscreen) e.webkitRequestFullscreen(); };
  $('#fs').onclick = fullscreen;
  document.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && !e.shiftKey && !e.altKey && (e.key === 'k' || e.key === 'K' || e.key === 'b' || e.key === 'B')) { e.preventDefault(); $('#sbtoggle').click(); } });
  const grip = $('#grip');
  grip.onmousedown = e => { e.preventDefault(); grip.classList.add('on'); document.body.classList.add('resizing'); const move = ev => { $('#side').style.width = Math.max(200, Math.min(innerWidth * 0.7, ev.clientX)) + 'px'; }; const up = () => { grip.classList.remove('on'); document.body.classList.remove('resizing'); removeEventListener('mousemove', move); removeEventListener('mouseup', up); }; addEventListener('mousemove', move); addEventListener('mouseup', up); };
)JS";

// The preferences: the widget a dropped address becomes, by type, and the size of a new control, by widget
inline constexpr char kScript2[] = R"JS(
  let prefs = { widgets: {}, sizes: {} }, prefOpts = null;
  const typeName = { float: 'Float', int: 'Integer', bool: 'Switch (on / off)', choice: 'Choice', trigger: 'Trigger' };
  const defSize = { vfader: [2, 5], fader: [4, 2], knob: [2, 2], number: [3, 2], slim: [2, 1], vslim: [1, 5], meter: [1, 5], hmeter: [4, 1], readout: [3, 2], graph: [4, 2], switch: [3, 2], led: [3, 2],
                    buttons: [6, 2], menu: [6, 2], hold: [3, 2], tap: [3, 2], picker: [3, 2], pad: [5, 5], adsr: [6, 3], multi: [8, 4], hmulti: [6, 5] };
  $('#prefs').onclick = () => {
    if (!prefOpts) return;
    const dlg = el('div'); dlg.id = 'dlg'; const box = el('div'); box.style.cssText = 'width:min(460px,92vw);max-height:84vh;overflow:auto;background:var(--panel2);border:1px solid var(--line);border-radius:12px;padding:16px 18px';
    const widgets = Object.assign({}, prefs.widgets), sizes = JSON.parse(JSON.stringify(prefs.sizes));
    let inspector = prefs.inspector || 'always', physics = prefs.physics || 'knob', pages = prefs.pages || 'shared';
    const save = () => send({ t: 'setPrefs', prefs: { widgets, sizes, inspector, physics, pages } });
    const head = t => { const h = el('div', '', t); h.style.cssText = 'font-weight:600;margin:12px 0 6px'; return h; };
    const row = (label, ...nodes) => { const r = el('div'); r.style.cssText = 'display:flex;align-items:center;gap:6px;margin:4px 0'; const l = el('span', '', label); l.style.flex = '1'; r.appendChild(l); nodes.forEach(n => r.appendChild(n)); box.appendChild(r); };
    box.appendChild(el('div', '', 'Preferences')).style.cssText = 'font-size:16px;font-weight:600';
    box.appendChild(head('Widget of a dropped address'));
    Object.keys(typeName).forEach(k => {
      const sel = el('select'); sel.appendChild(Object.assign(el('option', '', 'Automatic'), { value: '' }));
      (prefOpts.types[k] || []).forEach(w => sel.appendChild(Object.assign(el('option', '', wName[w] || w), { value: w })));
      sel.value = widgets[k] || ''; sel.onchange = () => { if (sel.value) widgets[k] = sel.value; else delete widgets[k]; save(); };
      row(typeName[k], sel);
    });
    box.appendChild(head('Controls page'));
    { const si = el('select'); [['always', 'Always shown in edit mode'], ['onDemand', 'Opens from a tile']].forEach(o => si.appendChild(Object.assign(el('option', '', o[1]), { value: o[0] }))); si.value = inspector; si.onchange = () => { inspector = si.value; save(); }; row('Inspector', si);
      const sp = el('select'); (prefOpts.types.float || []).forEach(w => sp.appendChild(Object.assign(el('option', '', wName[w] || w), { value: w }))); sp.value = physics; sp.onchange = () => { physics = sp.value; save(); }; row('Physics widget', sp); }
    { const sg = el('select'); [['shared', 'The same on every screen'], ['perScreen', 'One for each screen']].forEach(o => sg.appendChild(Object.assign(el('option', '', o[1]), { value: o[0] }))); sg.value = pages; sg.onchange = () => { pages = sg.value; save(); }; row('Current page', sg); }
    box.appendChild(head('Size of a new control (cells: width × height)'));
    prefOpts.sizeWidgets.forEach(w => {
      const d = defSize[w] || [3, 2], cur = sizes[w] || [], mk = i => { const n = el('input'); n.type = 'number'; n.min = 1; n.max = i ? 40 : 12; n.placeholder = String(d[i]); n.value = cur[i] || ''; n.style.width = '56px';
        n.onchange = () => { const a = [Number(box.querySelector('[data-w="' + w + '0"]').value) || d[0], Number(box.querySelector('[data-w="' + w + '1"]').value) || d[1]]; const blank = !box.querySelector('[data-w="' + w + '0"]').value && !box.querySelector('[data-w="' + w + '1"]').value; if (blank) delete sizes[w]; else sizes[w] = a; save(); };
        n.dataset.w = w + i; return n; };
      row(wName[w] || w, mk(0), el('span', '', '×'), mk(1));
    });
    const acts = el('div', 'acts'), reset = el('button', '', 'Reset'), close = el('button', 'on', 'Close'); acts.style.marginTop = '12px';
    reset.onclick = () => { send({ t: 'setPrefs', prefs: { widgets: {}, sizes: {} } }); dlg.remove(); };
    close.onclick = () => dlg.remove(); acts.appendChild(reset); acts.appendChild(close); box.appendChild(acts); dlg.appendChild(box); document.body.appendChild(dlg);
    dlg.onclick = e => { if (e.target === dlg) dlg.remove(); };
  };
  // The views: a whole layout kept under a name. Click one to show it; double-click to rename; the x deletes it
  function renderViews() {
    const box = $('#views'); box.textContent = '';
    box.appendChild(el('span', 'none', 'Views'));
    views.forEach(n => {
      const b = el('button', n === activeView ? 'on' : '', n); b.title = 'Show this view. Double-click to rename';
      b.onclick = () => send({ t: 'view', op: 'recall', name: n });
      b.ondblclick = () => { const to = prompt('Rename the view', n); if (to && to !== n) send({ t: 'view', op: 'rename', name: n, to }); };
      b.oncontextmenu = e => { e.preventDefault(); if (confirm('Delete the view "' + n + '"? The page stays as it is.')) send({ t: 'view', op: 'delete', name: n }); };
      box.appendChild(b);
    });
    const add = el('button', '', '+ View'); add.title = 'Keep the page as it is under a new name (right-click a view to delete it)';
    add.onclick = () => { const n = prompt('Name of the new view (a copy of this page)', 'View ' + (views.length + 1)); if (n !== null) send({ t: 'view', op: 'new', name: n }); };
    box.appendChild(add);
  }
  let views = [], activeView = '';
  renderViews();
  connect();
})();
)JS";

// The script is in three parts (a literal stays under 16 KB)
inline QByteArray script() { return QByteArray(kScript) + kScriptMid + kScript2; }

// The page as served, and the policy that lets only its script and style run, and frame the controls page
inline QByteArray page() {
    return QByteArray("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Kadran</title><link rel=\"icon\" href=\"") + logo::dataUri() + "\"><style>" +
           kStyle + "</style></head><body>" + kBody + "<script>" + script() + "</script></body></html>";
}

} // namespace shell_page
