'use strict';
// Renders the scene deterministically and writes PNG frames to shots/.
// usage: node_modules/.bin/electron scripts/capture.js

const { app, BrowserWindow } = require('electron');
const path = require('path');
const fs = require('fs');

const OUT = path.join(__dirname, '..', 'shots');
fs.mkdirSync(OUT, { recursive: true });

app.commandLine.appendSwitch('enable-transparent-visuals');

async function main() {
  const win = new BrowserWindow({
    width: 1280,
    height: 800,
    show: true, x: 30, y: 30, frame: false,
    backgroundColor: '#0e0c0b',
    webPreferences: { contextIsolation: true, nodeIntegration: false, offscreen: false }
  });

  await win.loadFile(path.join(__dirname, '..', 'renderer', 'index.html'));
  await win.webContents.executeJavaScript(`document.body.style.background = '#0e0c0b'; true`);

  const js = (code) => win.webContents.executeJavaScript(code);

  await js(`PX_test.begin({ text: "the pigeon never came.\\nso I sent something that burns.", from: "me", bird: "phoenix" })`);

  const wait = (ms) => new Promise((r) => setTimeout(r, ms));

  const shot = async (name) => {
    await wait(60);
    const img = await win.webContents.capturePage();
    fs.writeFileSync(path.join(OUT, name + '.png'), img.toPNG());
    const info = await js('JSON.stringify(PX_test.info())');
    console.log(name.padEnd(16), info);
  };

  // crop around a point and blow it up, so the small bird can be judged
  const zoom = async (name, w, h, factor) => {
    await wait(60);
    const info = JSON.parse(await js('JSON.stringify(PX_test.info())'));
    const x = Math.max(0, Math.round(info.x - w / 2));
    const y = Math.max(0, Math.round(info.y - h / 2));
    const img = await win.webContents.capturePage({ x, y, width: w, height: h });
    const big = img.resize({ width: w * factor, height: h * factor, quality: 'best' });
    fs.writeFileSync(path.join(OUT, name + '.png'), big.toPNG());
    console.log(name.padEnd(16), 'zoom @', x, y);
  };

  const domShot = async (name) => {
    await wait(60);
    const r = JSON.parse(await js(`(function(){const e=document.querySelector('.letter');if(!e)return 'null';const b=e.getBoundingClientRect();return JSON.stringify({x:Math.round(b.x),y:Math.round(b.y),w:Math.round(b.width),h:Math.round(b.height),op:getComputedStyle(e).opacity});})()`));
    console.log(name.padEnd(16), 'letter rect', JSON.stringify(r));
    const img = await win.webContents.capturePage();
    fs.writeFileSync(path.join(OUT, name + '.png'), img.toPNG());
  };

  const step = (n) => js(`PX_test.step(1/60, ${n})`);

  // entry + cruise
  await step(18);  await shot('01-entry'); await zoom('01z-entry', 260, 170, 4);
  await step(30);  await shot('02-cruise');
  await step(30);  await shot('03-approach'); await zoom('03z-approach', 260, 170, 4);

  // settle / drop / letter
  await js(`PX_test.skipTo('settle', 1800)`);   await shot('04-flare'); await zoom('04z-flare', 260, 170, 4);
  await js(`PX_test.skipTo('drop', 400)`);      await step(10); await shot('05-release');
  await js(`PX_test.skipTo('watch', 400)`);
  await wait(1400);                             // let the unfurl transition run
  await js('PX_test.step(1/60, 1)');
  await domShot('06-letter');

  // burn
  await js(`PX_test.skipTo('burn', 400)`);
  await step(24);  await shot('07-charge');
  await step(24);  await shot('08-charge2');
  await step(30);  await shot('09-ignite'); await zoom('09z-ignite', 300, 200, 3);
  await step(24);  await shot('10-ignite2'); await zoom('10z-ignite2', 300, 200, 3);
  await step(14);  await shot('11-flash');
  await step(20);  await shot('12-embers');
  await step(50);  await shot('13-ash');
  await step(80);  await shot('14-settle');

  win.destroy();
  app.quit();
}

app.whenReady().then(() => {
  main().catch((e) => { console.error('CAPTURE FAILED:', e); app.exit(1); });
});
