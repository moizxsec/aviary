'use strict';
const { app, BrowserWindow } = require('electron');
const path = require('path');
const fs = require('fs');

app.commandLine.appendSwitch('enable-transparent-visuals');

app.whenReady().then(async () => {
  const win = new BrowserWindow({
    width: 1500, height: 960, show: true, x: 40, y: 40,
    backgroundColor: '#0d0b0a', frame: false,
    webPreferences: { contextIsolation: true }
  });
  await win.loadFile(path.join(__dirname, '..', 'renderer', 'sheet.html'));
  await new Promise((r) => setTimeout(r, 700));
  const img = await win.webContents.capturePage();
  const out = path.join(__dirname, '..', 'shots', 'sheet.png');
  fs.writeFileSync(out, img.toPNG());
  console.log('wrote', out);
  win.destroy();
  app.quit();
});
