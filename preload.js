'use strict';
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('aviary', {
  onDeliver(cb) {
    ipcRenderer.on('aviary:deliver', (_e, letter) => cb(letter));
  },
  sceneDone() {
    ipcRenderer.send('aviary:scene-done');
  },
  setInteractive(on) {
    ipcRenderer.send('aviary:interactive', !!on);
  },
  setHitRect(rect) {
    ipcRenderer.send('aviary:hit-rect', rect);
  }
});
