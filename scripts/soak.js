'use strict';
// Steps a full delivery to completion, checking the scene actually finishes.
const { app, BrowserWindow } = require('electron');
const path = require('path');

app.commandLine.appendSwitch('enable-transparent-visuals');

app.whenReady().then(async () => {
  const win = new BrowserWindow({
    width: 1280, height: 800, show: true, x: 20, y: 20, frame: false,
    backgroundColor: '#0e0c0b', webPreferences: { contextIsolation: true }
  });
  await win.loadFile(path.join(__dirname, '..', 'renderer', 'index.html'));
  const js = (c) => win.webContents.executeJavaScript(c);
  const wait = (ms) => new Promise((r) => setTimeout(r, ms));

  let failures = 0;

  for (const trial of [
    { name: 'dismissed early', dismissAt: 8.0 },
    { name: 'left open',       dismissAt: 20.0 },
    { name: 'long message',    dismissAt: 9.0, text: 'x'.repeat(600) }
  ]) {
    await js(`PX_test.begin({ text: ${JSON.stringify(trial.text || 'test')}, from: 'me', bird: 'phoenix' })`);
    await wait(200);

    const seen = [];
    let t = 0;
    let done = false;
    let dismissed = false;

    for (let i = 0; i < 60 * 90; i++) {
      await js('PX_test.step(1/60, 6)');   // 6 sim frames per round trip
      t += 6 / 60;
      if (!dismissed && t >= trial.dismissAt) {
        dismissed = true;
        await js(`(function(){const b=document.querySelector('.letter-hint'); if(b) b.click(); return !!b;})()`);
      }
      const info = JSON.parse(await js('JSON.stringify(PX_test.info())'));
      if (!seen.length || seen[seen.length - 1].state !== info.state) {
        seen.push({ state: info.state, t: +t.toFixed(1) });
      }
      if (info.done) { done = true; break; }
      await wait(4);
    }

    const path_ = seen.map((s) => `${s.state}@${s.t}s`).join(' -> ');
    if (done) {
      console.log(`PASS  ${trial.name.padEnd(16)} ${path_}  (finished ${t.toFixed(1)}s)`);
    } else {
      failures++;
      console.log(`FAIL  ${trial.name.padEnd(16)} ${path_}  (never finished, ${t.toFixed(1)}s)`);
    }
  }

  console.log(failures ? `${failures} FAILURE(S)` : 'all scenes terminate');
  win.destroy();
  app.exit(failures ? 1 : 0);
});
