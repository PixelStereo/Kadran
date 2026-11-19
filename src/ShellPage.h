#pragma once
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
.bar { display:flex; align-items:center; padding:8px 10px; border-bottom:1px solid var(--line); gap:6px; }
.bar h1 { flex:1; margin:0; font-size:15px; font-weight:600; letter-spacing:.02em; }
.bar .dot { width:8px; height:8px; border-radius:50%; background:var(--bad); }
.bar .dot.ok { background:var(--ok); }
button, .btn { font:inherit; color:var(--text); background:var(--panel2); border:1px solid var(--line); border-radius:7px; padding:5px 10px; cursor:pointer; }
button:hover { border-color:var(--acc); }
button.on { background:var(--acc); border-color:var(--acc); color:#06222a; font-weight:600; }
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
    <div class="bar"><span class="dot" id="dot"></span><h1>Kadran</h1><button id="add" title="Add a device by its address">+ Device</button></div>
    <div class="bar"><button id="open">Open…</button><button id="save">Save</button><button id="undo" title="Undo (Cmd/Ctrl+Z)">↶</button><button id="redo" title="Redo (Shift+Cmd/Ctrl+Z)">↷</button><span style="flex:1"></span><button id="edit" title="Edit the page (Cmd/Ctrl+E)">Edit</button></div>
    <input id="find" type="search" placeholder="Filter the addresses" autocomplete="off">
    <div id="tree"></div>
    <div id="status"></div>
  </div>
  <div id="grip"></div>
  <div id="main"><iframe id="frame" src="/control" title="Controls"></iframe></div>
</div>
<input id="file" type="file" accept=".kadran,application/json" hidden>
<div id="menu" hidden></div>
)HTML";

inline constexpr char kScript[] = R"JS(
(() => {
  const $ = s => document.querySelector(s);
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
  const wName = { fader: 'Fader', vfader: 'Vertical fader', slim: 'Slim fader', vslim: 'Slim vertical fader', knob: 'Encoder', number: 'Number', switch: 'Switch', buttons: 'Buttons', menu: 'Menu', hold: 'Hold button', tap: 'Tap button' };
  let ws = null, devices = [], trees = {}, openNodes = new Set(), selected = new Map(), filter = '', lastRow = null, statusTimer = 0;
  const send = o => { if (ws && ws.readyState === 1) ws.send(JSON.stringify(o)); };
  const say = t => { $('#status').textContent = t || ''; clearTimeout(statusTimer); if (t) statusTimer = setTimeout(() => { $('#status').textContent = ''; }, 6000); };

  function connect() {
    ws = new WebSocket('ws://' + location.host + '/ws');
    ws.onopen = () => { $('#dot').classList.add('ok'); send({ t: 'hello' }); };
    ws.onclose = () => { $('#dot').classList.remove('ok'); setTimeout(connect, 1500); };
    ws.onmessage = e => {
      let m; try { m = JSON.parse(e.data); } catch (_) { return; }
      if (m.t === 'devices') { devices = m.list || []; render(); }
      else if (m.t === 'tree') { trees[m.device] = m.root || null; render(); }
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
    if (!devices.length) tree.appendChild(el('div', 'note', 'Looking for OSCQuery devices on the network… Add one by its address with + Device.'));
    devices.forEach(d => {
      const row = el('div', 'row dev' + (d.online ? '' : ' dead'));
      const k = key(d.id, '');
      const opened = openNodes.has(k);
      row.appendChild(el('span', 'arrow', opened ? '▼' : '▶'));
      row.appendChild(el('span', 'st ' + d.state));
      row.appendChild(el('span', 'name', d.name));
      row.title = d.address + (d.state === 'failed' ? ' (not answering)' : '');
      row.onclick = () => { if (openNodes.has(k)) openNodes.delete(k); else { openNodes.add(k); send({ t: 'open', device: d.id }); } render(); };
      tree.appendChild(row);
      if (opened) {
        const root = trees[d.id];
        if (!root) tree.appendChild(el('div', 'note', d.state === 'failed' ? 'Not answering. Trying again…' : 'Reading its namespace…'));
        else (root.c || []).forEach(n => nodeRows(tree, d.id, n, 1));
      }
    });
    tree.scrollTop = keep;
  }
  function nodeRows(into, dev, n, depth) {
    if (filter && !hasMatch(n, filter)) return;
    const k = key(dev, n.p), leaf = !n.c, can = !!n.w;
    const open = filter ? true : openNodes.has(k);
    const row = el('div', 'row' + (leaf ? '' : '') + (can ? '' : ' ro') + (selected.has(k) ? ' sel' : ''));
    row.style.paddingLeft = (10 + depth * 14) + 'px';
    row.appendChild(el('span', 'arrow', leaf ? '' : (open ? '▼' : '▶')));
    row.appendChild(el('span', 'name', n.n));
    if (n.t) row.appendChild(el('span', 'ty', n.t));
    row.title = n.p + (n.d ? '\n' + n.d : '') + (leaf && !can ? '\n(read only, or more than one argument: not a control yet)' : '');
    if (!leaf) { // a node: dragged, it brings all it holds, in a group of the page (Alt: in a page of its own)
      row.draggable = true;
      row.ondragstart = e => {
        const as = e.altKey ? 'page' : 'group';
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
    if (!leaf && open) n.c.forEach(c => nodeRows(into, dev, c, depth + 1));
  }
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
  function add(targets, widget, pad) { send({ t: 'add', targets, widget: widget || '', pad: !!pad, page: pageId() }); }
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
    }
    m.style.left = Math.min(e.clientX, innerWidth - 220) + 'px'; m.style.top = Math.min(e.clientY, innerHeight - m.offsetHeight - 8) + 'px';
  }
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
  $('#add').onclick = () => {
    const dlg = el('div'); dlg.id = 'dlg'; const form = el('form'); form.appendChild(el('div', '', 'Address of the OSCQuery server (host:port)'));
    const input = el('input'); input.placeholder = '192.168.1.20:5678'; form.appendChild(input);
    const acts = el('div', 'acts'), cancel = el('button', '', 'Cancel'), ok = el('button', 'on', 'Add'); cancel.type = 'button'; acts.appendChild(cancel); acts.appendChild(ok); form.appendChild(acts); dlg.appendChild(form); document.body.appendChild(dlg); input.focus();
    cancel.onclick = () => dlg.remove(); dlg.onclick = e => { if (e.target === dlg) dlg.remove(); };
    form.onsubmit = e => { e.preventDefault(); const v = input.value.trim(), i = v.lastIndexOf(':'); if (i > 0 && +v.slice(i + 1) > 0) send({ t: 'addDevice', host: v.slice(0, i), port: +v.slice(i + 1) }); else say('Give it as host:port'); dlg.remove(); };
  };
  const toggleEdit = () => { try { const b = frame.contentDocument.getElementById('edit'); if (b && !b.hidden) b.click(); $('#edit').classList.toggle('on', b && b.classList.contains('on')); } catch (_) {} };
  $('#edit').onclick = toggleEdit;
  document.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && !e.shiftKey && !e.altKey && (e.key === 'e' || e.key === 'E')) { e.preventDefault(); toggleEdit(); } });
  const grip = $('#grip');
  grip.onmousedown = e => { e.preventDefault(); grip.classList.add('on'); document.body.classList.add('resizing'); const move = ev => { $('#side').style.width = Math.max(200, Math.min(innerWidth * 0.7, ev.clientX)) + 'px'; }; const up = () => { grip.classList.remove('on'); document.body.classList.remove('resizing'); removeEventListener('mousemove', move); removeEventListener('mouseup', up); }; addEventListener('mousemove', move); addEventListener('mouseup', up); };
  connect();
})();
)JS";

// The page as served, and the policy that lets only its script and style run, and frame the controls page
inline QByteArray page() {
    return QByteArray("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Kadran</title><style>") +
           kStyle + "</style></head><body>" + kBody + "<script>" + kScript + "</script></body></html>";
}

} // namespace shell_page
