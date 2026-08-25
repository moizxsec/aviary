'use strict';

const { app, BrowserWindow, ipcMain, screen, Tray, Menu } = require('electron');
const path = require('path');
const http = require('http');
const { makeIcon } = require('./icon');

const PORT = Number(process.env.AVIARY_PORT || 45874);

// --- Linux transparency needs a compositor + this hint, set before app ready ---
app.commandLine.appendSwitch('enable-transparent-visuals');
if (process.platform === 'linux') {
  app.commandLine.appendSwitch('disable-features', 'UseOzonePlatform,WaylandWindowDecorations');
}

let win = null;
let tray = null;
let server = null;
let sceneBusy = false;
let interactive = false;
let hitRect = null;
let cursorTimer = null;
const queue = [];

// ---------------------------------------------------------------- window ----

function createWindow() {
  const display = screen.getPrimaryDisplay();
  const b = display.bounds;

  win = new BrowserWindow({
    x: b.x,
    y: b.y,
    width: b.width,
    height: b.height,
    transparent: true,
    frame: false,
    resizable: false,
    movable: false,
    hasShadow: false,
    skipTaskbar: true,
    show: false,
    alwaysOnTop: true,
    fullscreenable: false,
    backgroundColor: '#00000000',
    type: process.platform === 'linux' ? 'normal' : undefined,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false
    }
  });

  win.setAlwaysOnTop(true, 'screen-saver');
  win.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true });
  win.setIgnoreMouseEvents(true, { forward: true });
  win.loadFile(path.join(__dirname, 'renderer', 'index.html'));

  win.on('closed', () => { win = null; });
}

function showStage() {
  if (!win) return;
  if (!win.isVisible()) {
    win.showInactive();
    win.setAlwaysOnTop(true, 'screen-saver');
  }
}

function hideStage() {
  if (win && win.isVisible()) win.hide();
}

// ---------------------------------------------------------------- deliver ---

function deliver(letter) {
  if (!win) return;
  if (sceneBusy) { queue.push(letter); return; }
  sceneBusy = true;
  showStage();
  const send = () => win.webContents.send('aviary:deliver', letter);
  if (win.webContents.isLoading()) win.webContents.once('did-finish-load', send);
  else send();
}

ipcMain.on('aviary:scene-done', () => {
  sceneBusy = false;
  stopCursorWatch();
  setInteractive(false);
  if (queue.length) {
    setTimeout(() => deliver(queue.shift()), 600);
  } else {
    hideStage();
  }
});

function setInteractive(on) {
  if (!win) return;
  if (interactive === on) return;
  interactive = on;
  win.setIgnoreMouseEvents(!on, { forward: true });
}

ipcMain.on('aviary:interactive', (_e, on) => setInteractive(!!on));

// The overlay covers the whole screen, so it must stay click-through except
// while the pointer is genuinely over the letter.
//
// On Windows/macOS the renderer can do this itself, because ignored mouse
// events are still forwarded to it. On Linux they are NOT forwarded, so the
// renderer goes blind the moment we ignore -- and the letter's button would
// be unclickable forever. Poll the cursor from the main process instead.
ipcMain.on('aviary:hit-rect', (_e, rect) => {
  hitRect = rect;
  if (rect) startCursorWatch();
  else {
    stopCursorWatch();
    setInteractive(false);
  }
});

function startCursorWatch() {
  if (cursorTimer) return;
  cursorTimer = setInterval(() => {
    if (!win || !hitRect) return;
    const p = screen.getCursorScreenPoint();
    const b = win.getBounds();
    const x = p.x - b.x;
    const y = p.y - b.y;
    setInteractive(
      x >= hitRect.x && x <= hitRect.x + hitRect.w &&
      y >= hitRect.y && y <= hitRect.y + hitRect.h
    );
  }, 60);
}

function stopCursorWatch() {
  if (!cursorTimer) return;
  clearInterval(cursorTimer);
  cursorTimer = null;
  hitRect = null;
}

// ----------------------------------------------------------------- server ---

const CONTROL_PAGE = `<!doctype html><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aviary</title>
<style>
 body{margin:0;min-height:100vh;display:grid;place-items:center;background:#140c08;
      color:#ffd98a;font:16px/1.5 Georgia,serif}
 form{width:min(92vw,420px);display:grid;gap:12px}
 h1{font-size:20px;letter-spacing:.14em;text-transform:uppercase;margin:0;color:#ff8c1a}
 textarea,input,select,button{font:inherit;padding:12px;border-radius:8px;border:1px solid #5a2a12;
      background:#20120b;color:#ffe6a3}
 textarea{min-height:120px;resize:vertical}
 button{background:linear-gradient(#ff8c1a,#c22a0d);color:#fff7e2;border:0;font-weight:700;
      letter-spacing:.08em;text-transform:uppercase;cursor:pointer}
</style>
<form method="GET" action="/send">
 <h1>&#128293; Send a phoenix</h1>
 <input name="from" placeholder="from" value="">
 <textarea name="text" placeholder="the letter&hellip;"></textarea>
 <select name="bird"><option value="phoenix">phoenix</option></select>
 <button>Release the bird</button>
</form>`;

function startServer() {
  server = http.createServer((req, res) => {
    const url = new URL(req.url, `http://127.0.0.1:${PORT}`);

    const finish = (code, body, type = 'text/plain; charset=utf-8') => {
      res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store' });
      res.end(body);
    };

    if (url.pathname === '/' ) return finish(200, CONTROL_PAGE, 'text/html; charset=utf-8');
    if (url.pathname === '/health') return finish(200, 'ok');

    if (url.pathname === '/send') {
      const run = (params) => {
        const text = (params.get('text') || '').slice(0, 4000).trim();
        if (!text) return finish(400, 'empty letter');
        deliver({
          text,
          from: (params.get('from') || '').slice(0, 60).trim(),
          bird: (params.get('bird') || 'phoenix').toLowerCase(),
          at: Date.now()
        });
        finish(200, 'released');
      };

      if (req.method === 'POST') {
        let raw = '';
        req.on('data', (c) => { raw += c; if (raw.length > 8192) req.destroy(); });
        req.on('end', () => {
          let params;
          try {
            params = raw.trim().startsWith('{')
              ? new URLSearchParams(Object.entries(JSON.parse(raw)).map(([k, v]) => [k, String(v)]))
              : new URLSearchParams(raw);
          } catch { return finish(400, 'bad body'); }
          run(params);
        });
        return;
      }
      return run(url.searchParams);
    }

    finish(404, 'no such perch');
  });

  server.on('error', (err) => {
    console.error('[aviary] control server:', err.message);
  });

  // loopback only — nothing on this box is exposed to the network
  server.listen(PORT, '127.0.0.1', () => {
    console.log(`[aviary] listening on http://127.0.0.1:${PORT}`);
  });
}

// ------------------------------------------------------------------- tray ---

function startTray() {
  try {
    tray = new Tray(makeIcon(22));
    tray.setToolTip('Aviary — phoenix post');
    tray.setContextMenu(Menu.buildFromTemplate([
      {
        label: 'Test flight',
        click: () => deliver({
          text: 'I am sorry the pigeon never came.\nSo I sent something that burns instead.',
          from: 'me',
          bird: 'phoenix',
          at: Date.now()
        })
      },
      { type: 'separator' },
      { label: `Control page :${PORT}`, enabled: false },
      { type: 'separator' },
      { label: 'Quit', click: () => app.quit() }
    ]));
  } catch (err) {
    console.warn('[aviary] tray unavailable:', err.message);
  }
}

// ------------------------------------------------------------------- boot ---

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on('second-instance', (_e, argv) => {
    const text = argv.slice(2).filter((a) => !a.startsWith('-')).join(' ');
    if (text) deliver({ text, from: '', bird: 'phoenix', at: Date.now() });
  });

  app.whenReady().then(() => {
    // small delay: X11 compositors need a beat before a transparent surface sticks
    setTimeout(() => {
      createWindow();
      startTray();
      startServer();
    }, process.platform === 'linux' ? 300 : 0);
  });

  app.on('window-all-closed', (e) => { e.preventDefault(); });
  app.on('before-quit', () => {
    stopCursorWatch();
    if (server) server.close();
  });
}
