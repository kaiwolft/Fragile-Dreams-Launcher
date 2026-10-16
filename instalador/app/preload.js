// Puente seguro entre la interfaz (Chromium) y el proceso principal (Node)
const { contextBridge, ipcRenderer } = require('electron');
const listeners = [];
ipcRenderer.on('msg', (_e, data) => listeners.forEach((cb) => cb({ data })));
contextBridge.exposeInMainWorld('ptHost', {
  postMessage: (s) => ipcRenderer.send('msg', s),
  addEventListener: (type, cb) => { if (type === 'message') listeners.push(cb); }
});
