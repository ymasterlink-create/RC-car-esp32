#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

// ==========================================
// CONFIGURATION
// ==========================================
const char* ssid = "RC_Pro_Controller";
const char* password = ""; // Open network

// ==========================================
// MOTOR SETTINGS & PWM
// ==========================================
#define MAX_SPEED 240      // 0-255.
#define PWM_FREQ 1000
#define PWM_RES 8

// ==========================================
// PIN DEFINITIONS (REMAPPED)
// ==========================================
#define IN1 25   // Left Motor Forward
#define IN2 26   // Left Motor Backward
#define IN3 32   // Right Motor Forward
#define IN4 33   // Right Motor Backward

#define PIN_HAZARD1 27    
#define PIN_HAZARD2 14    
#define PIN_HEADLIGHTS2 13  
#define PIN_HEADLIGHTS 12   

#define CH_IN1 0
#define CH_IN2 1
#define CH_IN3 2
#define CH_IN4 3

// ==========================================
// GLOBALS
// ==========================================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;

int targetSpeedLeft = 0;  
int targetSpeedRight = 0; 
bool motorInvertLeft  = false; 
bool motorInvertRight = false; 
bool hazardActive = false;      
bool hazardState  = false;      
bool strobeActive = false;      

unsigned long hazardLastToggle = 0; 
int hazardPatternStep = 0;      
const unsigned long HAZARD_INTERVAL_DEFAULT = 350; 

// ==========================================
// HTML / CSS / JS (Frontend)
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>ESP32 RC Pro</title>
<style>
  :root { 
    --bg: #0b1121; 
    --panel: #151e32; 
    --surface: #1e293b; 
    --border: #334155;
    --accent: #3b82f6; 
    --accent-dim: rgba(59, 130, 246, 0.1);
    --text: #94a3b8;
    --text-light: #f8fafc;
    --shadow-btn: 0 6px 0 0 #0f172a;
    --shadow-btn-active: 0 0 0 0 transparent;
  }

  body.light-mode {
    --bg: #f1f5f9;
    --panel: #ffffff;
    --surface: #e2e8f0;
    --border: #cbd5e1;
    --accent: #2563eb;
    --accent-dim: rgba(37, 99, 235, 0.1);
    --text: #475569;
    --text-light: #0f172a;
    --shadow-btn: 0 6px 0 0 #94a3b8;
  }
  
  * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
  
  body {
    margin: 0; padding: 0; background-color: var(--bg); color: var(--text);
    font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
    overflow: hidden; height: 100vh; width: 100vw;
    display: flex; flex-direction: column; user-select: none;
    background-image: radial-gradient(var(--border) 1px, transparent 1px);
    background-size: 24px 24px;
    touch-action: none;
    transition: background-color 0.3s, color 0.3s;
  }

  /* --- HEADER --- */
  header {
    height: 64px; background: rgba(var(--bg), 0.8); backdrop-filter: blur(10px);
    display: flex; align-items: center; justify-content: space-between; padding: 0 16px;
    border-bottom: 1px solid var(--border); z-index: 50;
  }

  .header-left { display: flex; align-items: center; gap: 8px; }
  
  .status-dot {
    width: 36px; height: 36px; border-radius: 50%;
    display: flex; align-items: center; justify-content: center;
    background: rgba(239, 68, 68, 0.1); color: #ef4444;
    transition: 0.3s;
  }
  .status-dot.connected { background: rgba(34, 197, 94, 0.1); color: #22c55e; }

  .ip-box {
    display: none; background: var(--panel); border: 1px solid var(--border);
    border-radius: 8px; padding: 0 8px; height: 36px;
    align-items: center; width: 140px;
  }
  @media (min-width: 500px) { .ip-box { display: flex; } }
  
  .ip-input {
    background: transparent; border: none; outline: none;
    color: var(--text-light); font-family: monospace; font-size: 12px; width: 100%;
  }

  .btn-connect {
    background: #22c55e; color: white; border: none;
    height: 36px; padding: 0 15px; border-radius: 8px;
    font-weight: bold; font-size: 13px; cursor: pointer;
    box-shadow: 0 4px 10px rgba(34, 197, 94, 0.2);
    transition: 0.2s;
  }
  .btn-connect.disconnect { background: #ef4444; }

  .header-right { display: flex; align-items: center; gap: 6px; }

  .btn-tool {
    background: var(--panel); border: 1px solid var(--border);
    color: var(--text); height: 36px; width: 36px;
    border-radius: 8px; display: flex; align-items: center; justify-content: center;
    cursor: pointer; transition: 0.2s;
  }
  .btn-tool.active { background: var(--accent); border-color: var(--accent); color: white; }
  .btn-edit-text { width: auto; padding: 0 12px; gap: 6px; font-size: 12px; font-weight: bold; }

  /* --- DASHBOARD --- */
  .dashboard-pill {
    top: 80px; left: 50%; transform: translateX(-50%);
    background: var(--panel); border: 1px solid var(--border);
    border-radius: 30px; padding: 8px 20px;
    display: flex; align-items: center; gap: 15px;
    box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.2);
    z-index: 40; 
  }
  .dash-item { display: flex; flex-direction: column; align-items: center; gap: 4px; }
  .dash-label { font-size: 8px; font-weight: bold; color: #64748b; }
  
  .dash-btn {
    background: var(--surface); border: 1px solid var(--border);
    width: 34px; height: 34px; border-radius: 10px;
    display: flex; align-items: center; justify-content: center;
    color: #64748b; cursor: pointer;
  }
  .dash-btn.active-light { background: rgba(234, 179, 8, 0.1); border-color: #eab308; color: #eab308; }
  .dash-btn.active-hazard { background: rgba(239, 68, 68, 0.1); border-color: #ef4444; color: #ef4444; animation: pulse 1s infinite; }
  .dash-btn.active-strobe { background: rgba(168, 85, 247, 0.1); border-color: #a855f7; color: #a855f7; }

  .divider { width: 1px; height: 24px; background: var(--border); }

  /* --- GAME AREA --- */
  #game-area { flex: 1; position: relative; width: 100%; height: 100%; }
  .group { position: absolute; display: flex; align-items: center; justify-content: center; }

  .control-box {
    background: rgba(var(--panel), 0.8); border: 1px solid var(--border);
    backdrop-filter: blur(8px);
    border-radius: 40px; padding: 16px;
    display: flex; gap: 16px;
  }

  /* --- 3D BUTTONS --- */
  .btn-3d {
    width: 100px; height: 100px;
    background: var(--surface); border: 1px solid var(--border);
    border-radius: 24px; color: var(--text);
    display: flex; align-items: center; justify-content: center;
    font-size: 32px; cursor: pointer;
    box-shadow: var(--shadow-btn);
    transition: all 0.1s; touch-action: manipulation;
  }
  .btn-3d:active, .btn-3d.pressed {
    transform: translateY(6px); box-shadow: var(--shadow-btn-active);
    background: var(--accent); color: white; border-color: var(--accent);
  }

  /* Log Box */
  #log-box {
    width: 300px; height: 180px;
    background: var(--panel); border: 1px solid var(--border);
    border-radius: 16px; display: flex; flex-direction: column;
    overflow: hidden;
  }
  .log-header {
    height: 36px; border-bottom: 1px solid var(--border);
    display: flex; align-items: center; padding: 0 12px; gap: 8px;
    font-size: 11px; font-weight: bold; background: rgba(0,0,0,0.05);
  }
  .log-content { flex: 1; padding: 12px; overflow-y: auto; font-family: monospace; font-size: 11px; }
  .log-tx { color: #3b82f6; } .log-err { color: #ef4444; } .log-suc { color: #22c55e; }

  /* Overlays */
  .resize-handle { width: 24px; height: 24px; background: var(--accent); border: 2px solid white; border-radius: 50%; position: absolute; bottom: -8px; right: -8px; display: none; z-index: 100; }
  .edit-overlay { position: absolute; inset: 0; border: 2px dashed var(--accent); background: var(--accent-dim); border-radius: 40px; pointer-events: none; display: none; z-index: 90; }
  .edit-mode .resize-handle, .edit-mode .edit-overlay { display: block; }
  .edit-mode .draggable { cursor: move; }

  @keyframes pulse { 50% { opacity: 0.5; } }
</style>
</head>
<body id="body-main">

<header>
  <div class="header-left">
    <div id="status-icon" class="status-dot">
      <!-- Default Disconnected Icon with Cross -->
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M1 1l22 22"/><path d="M16.72 11.06A10.94 10.94 0 0 1 19 12.55"/><path d="M5 12.55a10.94 10.94 0 0 1 5.17-2.39"/><path d="M10.71 5.05A16 16 0 0 1 22.58 9"/><path d="M1.42 9a15.91 15.91 0 0 1 4.7-2.88"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/></svg>
    </div>
    <div class="ip-box">
      <input type="text" id="ip-address" value="192.168.4.1" class="ip-input">
    </div>
    <button id="btn-connect" class="btn-connect" onclick="toggleConnection()">Connect</button>
  </div>
  
  <div class="header-right">
    <button id="btn-theme" class="btn-tool" onclick="toggleTheme()" title="Toggle Theme">
      <svg id="theme-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <circle cx="12" cy="12" r="5"></circle>
        <line x1="12" y1="1" x2="12" y2="3"></line>
        <line x1="12" y1="21" x2="12" y2="23"></line>
        <line x1="4.22" y1="4.22" x2="5.64" y2="5.64"></line>
        <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"></line>
        <line x1="1" y1="12" x2="3" y2="12"></line>
        <line x1="21" y1="12" x2="23" y2="12"></line>
        <line x1="4.22" y1="19.78" x2="5.64" y2="18.36"></line>
        <line x1="18.36" y1="5.64" x2="19.78" y2="4.22"></line>
      </svg>
    </button>

    <button id="btn-reset" class="btn-tool" style="display:none; color:#ef4444" onclick="resetLayout()">
       <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/></svg>
    </button>
    <button id="btn-edit" class="btn-tool btn-edit-text" onclick="toggleEdit()">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
      <span id="txt-edit">Edit</span>
    </button>
  </div>
</header>

<div id="game-area">
  <div id="grp-dash" class="dashboard-pill group draggable">
    <div class="edit-overlay" style="border-radius: 30px;"></div>
    <div class="dash-item"><span class="dash-label">LIGHTS</span>
      <button id="btn-light" class="dash-btn" onclick="toggleParams('LIGHT')"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18h6"/><path d="M10 22h4"/><path d="M12 2v4"/><path d="M12 14a4 4 0 1 0 0-8 4 4 0 0 0 0 8z"/></svg></button>
    </div>
    <div class="divider"></div>
    <div class="dash-item"><span class="dash-label">HAZARD</span>
      <button id="btn-hazard" class="dash-btn" onclick="toggleParams('HAZARD')"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg></button>
    </div>
    <div class="divider"></div>
    <div class="dash-item"><span class="dash-label">STROBE</span>
      <button id="btn-strobe" class="dash-btn" onclick="toggleParams('STROBE')"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"></polygon></svg></button>
    </div>
  </div>

  <div id="grp-log" class="group draggable" style="top: 50%; left: 50%; transform: translate(-50%, -50%);">
    <div id="log-box">
      <div class="log-header"><span style="color:var(--accent)">>_</span> SYSTEM LOG</div>
      <div id="log-content" class="log-content"><div class="log-msg" style="opacity:0.5">Ready...</div></div>
    </div>
    <div class="edit-overlay" style="border-radius:16px"></div>
    <div class="resize-handle" onmousedown="startResize(event, 'grp-log')" ontouchstart="startResize(event, 'grp-log')"></div>
  </div>

  <div id="grp-steer" class="group draggable" style="left: 40px; bottom: 40px;">
    <div class="control-box">
      <button class="btn-3d" data-cmd="LEFT"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5"/><path d="M12 19l-7-7 7-7"/></svg></button>
      <button class="btn-3d" data-cmd="RIGHT"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12h14"/><path d="M12 5l7 7-7 7"/></svg></button>
    </div>
    <div class="edit-overlay"></div>
    <div class="resize-handle" onmousedown="startResize(event, 'grp-steer')" ontouchstart="startResize(event, 'grp-steer')"></div>
  </div>

  <div id="grp-thro" class="group draggable" style="right: 40px; bottom: 40px;">
    <div class="control-box" style="flex-direction: column;">
      <button class="btn-3d" data-cmd="FORWARD"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 19V5"/><path d="M5 12l7-7 7 7"/></svg></button>
      <button class="btn-3d" data-cmd="BACKWARD"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 5v14"/><path d="M19 12l-7 7-7-7"/></svg></button>
    </div>
    <div class="edit-overlay"></div>
    <div class="resize-handle" onmousedown="startResize(event, 'grp-thro')" ontouchstart="startResize(event, 'grp-thro')"></div>
  </div>
</div>

<script>
  let isConnected = false;
  let isEditMode = false;
  let websocket;
  let lightsOn = false, hazardsOn = false, strobeOn = false;

  function toggleTheme() {
    const body = document.getElementById('body-main');
    const isLight = body.classList.toggle('light-mode');
    localStorage.setItem('rc_theme', isLight ? 'light' : 'dark');
    updateThemeIcon(isLight);
  }

  function updateThemeIcon(isLight) {
    const icon = document.getElementById('theme-icon');
    if (isLight) {
      icon.innerHTML = '<path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path>'; // Moon
    } else {
      icon.innerHTML = '<circle cx="12" cy="12" r="5"></circle><line x1="12" y1="1" x2="12" y2="3"></line><line x1="12" y1="21" x2="12" y2="23"></line><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"></line><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"></line><line x1="1" y1="12" x2="3" y2="12"></line><line x1="21" y1="12" x2="23" y2="12"></line><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"></line><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"></line>'; // Sun
    }
  }

  function addLog(msg, type='info') {
    const box = document.getElementById('log-content');
    const div = document.createElement('div');
    div.className = 'log-msg ' + (type === 'tx' ? 'log-tx' : type === 'error' ? 'log-err' : type === 'success' ? 'log-suc' : '');
    div.innerHTML = `<span>[${new Date().toLocaleTimeString([],{hour12:false,hour:'2-digit',minute:'2-digit',second:'2-digit'})}]</span> ${msg}`;
    box.appendChild(div); box.scrollTop = box.scrollHeight;
  }

  function toggleConnection() {
    if(isConnected) { websocket.close(); onDisconnect(); }
    else {
      const ip = document.getElementById('ip-address').value;
      websocket = new WebSocket(`ws://${ip}/ws`);
      websocket.onopen = () => { isConnected = true; updateUIState(true); addLog(`Connected to ${ip}`, 'success'); };
      websocket.onclose = () => onDisconnect();
      websocket.onerror = () => addLog('Connection Error', 'error');
    }
  }

  function onDisconnect() { isConnected = false; updateUIState(false); addLog('Disconnected', 'error'); }

  function updateUIState(connected) {
    const btn = document.getElementById('btn-connect');
    const icon = document.getElementById('status-icon');
    if(connected) {
      btn.innerText = 'Disconnect'; 
      btn.classList.add('disconnect'); 
      icon.classList.add('connected');
      // Update SVG to Connected state (Remove cross)
      icon.innerHTML = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/></svg>';
    } else {
      btn.innerText = 'Connect'; 
      btn.classList.remove('disconnect'); 
      icon.classList.remove('connected');
      // Update SVG to Disconnected state (Add cross)
      icon.innerHTML = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M1 1l22 22"/><path d="M16.72 11.06A10.94 10.94 0 0 1 19 12.55"/><path d="M5 12.55a10.94 10.94 0 0 1 5.17-2.39"/><path d="M10.71 5.05A16 16 0 0 1 22.58 9"/><path d="M1.42 9a15.91 15.91 0 0 1 4.7-2.88"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/></svg>';
    }
  }

  function sendCmd(cmd, val) {
    if(websocket && isConnected) {
      websocket.send(JSON.stringify({cmd: cmd, val: val}));
      if(val === 1) addLog(`TX: ${cmd}`, 'tx');
    }
  }

  function toggleParams(type) {
    if(type === 'LIGHT') { lightsOn = !lightsOn; document.getElementById('btn-light').classList.toggle('active-light', lightsOn); sendCmd('LIGHT_' + (lightsOn ? 'ON' : 'OFF'), 1); } 
    else if (type === 'HAZARD') { hazardsOn = !hazardsOn; document.getElementById('btn-hazard').classList.toggle('active-hazard', hazardsOn); sendCmd('HAZARD_' + (hazardsOn ? 'ON' : 'OFF'), 1); }
    else if (type === 'STROBE') { strobeOn = !strobeOn; document.getElementById('btn-strobe').classList.toggle('active-strobe', strobeOn); sendCmd('STROBE_' + (strobeOn ? 'ON' : 'OFF'), 1); }
  }

  document.querySelectorAll('.btn-3d').forEach(btn => {
    const cmd = btn.dataset.cmd;
    const press = (e) => { if(isEditMode) return; e.preventDefault(); btn.classList.add('pressed'); sendCmd(cmd, 1); };
    const release = (e) => { if(isEditMode) return; e.preventDefault(); btn.classList.remove('pressed'); sendCmd(cmd, 0); };
    btn.addEventListener('touchstart', press, {passive: false}); btn.addEventListener('touchend', release);
    btn.addEventListener('mousedown', press); btn.addEventListener('mouseup', release); btn.addEventListener('mouseleave', release);
  });

  function toggleEdit() {
    isEditMode = !isEditMode;
    document.body.classList.toggle('edit-mode');
    document.getElementById('btn-edit').classList.toggle('active', isEditMode);
    document.getElementById('txt-edit').innerText = isEditMode ? 'Done' : 'Edit';
    document.getElementById('btn-reset').style.display = isEditMode ? 'flex' : 'none';
    if(!isEditMode) saveLayout();
  }

  function saveLayout() {
    const layout = {};
    document.querySelectorAll('.draggable').forEach(el => {
      layout[el.id] = { left: el.style.left, top: el.style.top, transform: el.style.transform };
    });
    const logBox = document.getElementById('log-box');
    layout['size_log'] = { w: logBox.style.width, h: logBox.style.height };
    localStorage.setItem('rc_layout_v2', JSON.stringify(layout));
  }

  function loadLayout() {
    const theme = localStorage.getItem('rc_theme');
    if(theme === 'light') { document.getElementById('body-main').classList.add('light-mode'); updateThemeIcon(true); }
    const data = localStorage.getItem('rc_layout_v2');
    if(!data) return;
    const layout = JSON.parse(data);
    for (const id in layout) {
      if(id === 'size_log') {
        const lb = document.getElementById('log-box');
        if(layout[id].w) lb.style.width = layout[id].w;
        if(layout[id].h) lb.style.height = layout[id].h;
        continue;
      }
      const el = document.getElementById(id);
      if(el) { el.style.left = layout[id].left; el.style.top = layout[id].top; el.style.transform = layout[id].transform; }
    }
  }

  function resetLayout() { if(confirm('Reset layout?')) { localStorage.removeItem('rc_layout_v2'); location.reload(); } }

  let dragItem = null, startX, startY, startLeft, startTop, isResizing = false;
  document.querySelectorAll('.draggable').forEach(el => {
    const startDrag = (e) => {
      if(!isEditMode || e.target.classList.contains('resize-handle')) return;
      e.preventDefault(); dragItem = el;
      const t = e.touches ? e.touches[0] : e;
      startX = t.clientX; startY = t.clientY; startLeft = el.offsetLeft; startTop = el.offsetTop;
      el.style.transform = 'none';
    };
    el.addEventListener('touchstart', startDrag, {passive:false}); el.addEventListener('mousedown', startDrag);
  });

  const onMove = (e) => {
    if(!dragItem || isResizing) return;
    const t = e.touches ? e.touches[0] : e;
    dragItem.style.left = (startLeft + t.clientX - startX) + 'px';
    dragItem.style.top = (startTop + t.clientY - startY) + 'px';
    dragItem.style.bottom = 'auto'; dragItem.style.right = 'auto';
  };
  window.addEventListener('mousemove', onMove);
  window.addEventListener('touchmove', onMove, {passive:false});
  window.addEventListener('mouseup', () => dragItem = null);
  window.addEventListener('touchend', () => dragItem = null);

  function startResize(e, id) {
    if(!isEditMode) return; e.stopPropagation(); isResizing = true;
    const t = e.touches ? e.touches[0] : e;
    const initX = t.clientX, initY = t.clientY;
    const el = document.getElementById(id);
    const box = (id === 'grp-log') ? document.getElementById('log-box') : el.querySelector('.btn-3d');
    const sW = box.offsetWidth, sH = box.offsetHeight;
    const doResize = (ev) => {
      const ct = ev.touches ? ev.touches[0] : ev;
      if(id === 'grp-log') {
        box.style.width = Math.max(200, sW + ct.clientX - initX) + 'px';
        box.style.height = Math.max(100, sH + ct.clientY - initY) + 'px';
      } else {
        const size = Math.max(60, sW + ct.clientX - initX);
        el.querySelectorAll('.btn-3d').forEach(b => { 
          b.style.width = size + 'px'; b.style.height = size + 'px'; 
          const svg = b.querySelector('svg'); if(svg) { svg.setAttribute('width', size*0.4); svg.setAttribute('height', size*0.4); }
        });
      }
    };
    const stop = () => { 
      window.removeEventListener('mousemove', doResize); 
      window.removeEventListener('touchmove', doResize);
      isResizing = false; 
    };
    window.addEventListener('mousemove', doResize);
    window.addEventListener('touchmove', doResize, {passive:false});
    window.addEventListener('mouseup', stop);
    window.addEventListener('touchend', stop);
  }

  window.onload = loadLayout;
</script>
</body>
</html>
)rawliteral";

// --- WEBSOCKET EVENT HANDLING ---
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
              AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("Client Connected");
  }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      String msg = (char*)data;
      String cmd = "";
      int val = 0;

      int cmdStart = msg.indexOf("\"cmd\":\"") + 7;
      int cmdEnd   = msg.indexOf("\"", cmdStart);
      cmd = msg.substring(cmdStart, cmdEnd);

      int valStart = msg.indexOf("\"val\":") + 6;
      int valEnd   = msg.indexOf("}", valStart);
      val = msg.substring(valStart, valEnd).toInt();

      if (cmd == "RIGHT") {
        if (val) { targetSpeedLeft = MAX_SPEED; targetSpeedRight = -MAX_SPEED; }
        else { targetSpeedLeft = 0; targetSpeedRight = 0; }
      }
      else if (cmd == "LEFT") {
        if (val) { targetSpeedLeft = -MAX_SPEED; targetSpeedRight = MAX_SPEED; }
        else { targetSpeedLeft = 0; targetSpeedRight = 0; }
      }
      else if (cmd == "FORWARD") {
        if (val) { targetSpeedLeft = -MAX_SPEED; targetSpeedRight = -MAX_SPEED; }
        else { targetSpeedLeft = 0; targetSpeedRight = 0; }
      }
      else if (cmd == "BACKWARD") {
        if (val) { targetSpeedLeft = MAX_SPEED; targetSpeedRight = MAX_SPEED; }
        else { targetSpeedLeft = 0; targetSpeedRight = 0; }
      }
      else if (cmd == "LIGHT_ON") { digitalWrite(PIN_HEADLIGHTS, LOW); digitalWrite(PIN_HEADLIGHTS2, LOW); }
      else if (cmd == "LIGHT_OFF") { digitalWrite(PIN_HEADLIGHTS, HIGH); digitalWrite(PIN_HEADLIGHTS2, HIGH); }
      else if (cmd == "HAZARD_ON") { hazardActive = true; hazardLastToggle = millis(); hazardPatternStep = 0; }
      else if (cmd == "HAZARD_OFF") { hazardActive = false; }
      else if (cmd == "STROBE_ON") { strobeActive = true; hazardPatternStep = 0; hazardLastToggle = millis(); }
      else if (cmd == "STROBE_OFF") { strobeActive = false; }
    }
  }
}

void setMotorSpeed(int speedL, int speedR) {
  if (speedL > 0) { ledcWrite(CH_IN1, speedL); ledcWrite(CH_IN2, 0); } 
  else if (speedL < 0) { ledcWrite(CH_IN1, 0); ledcWrite(CH_IN2, -speedL); } 
  else { ledcWrite(CH_IN1, 0); ledcWrite(CH_IN2, 0); }

  if (speedR > 0) { ledcWrite(CH_IN3, speedR); ledcWrite(CH_IN4, 0); } 
  else if (speedR < 0) { ledcWrite(CH_IN3, 0); ledcWrite(CH_IN4, -speedR); } 
  else { ledcWrite(CH_IN3, 0); ledcWrite(CH_IN4, 0); }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_HAZARD1, OUTPUT); pinMode(PIN_HAZARD2, OUTPUT);
  pinMode(PIN_HEADLIGHTS, OUTPUT); pinMode(PIN_HEADLIGHTS2, OUTPUT);
  digitalWrite(PIN_HAZARD1, HIGH); digitalWrite(PIN_HAZARD2, HIGH);
  digitalWrite(PIN_HEADLIGHTS, HIGH); digitalWrite(PIN_HEADLIGHTS2, HIGH);

  pinMode(IN1, OUTPUT); digitalWrite(IN1, LOW);
  pinMode(IN2, OUTPUT); digitalWrite(IN2, LOW);
  pinMode(IN3, OUTPUT); digitalWrite(IN3, LOW);
  pinMode(IN4, OUTPUT); digitalWrite(IN4, LOW);

  ledcSetup(CH_IN1, PWM_FREQ, PWM_RES); ledcSetup(CH_IN2, PWM_FREQ, PWM_RES);
  ledcSetup(CH_IN3, PWM_FREQ, PWM_RES); ledcSetup(CH_IN4, PWM_FREQ, PWM_RES);
  ledcAttachPin(IN1, CH_IN1); ledcAttachPin(IN2, CH_IN2);
  ledcAttachPin(IN3, CH_IN3); ledcAttachPin(IN4, CH_IN4);

  WiFi.softAP(ssid, password);
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
  server.onNotFound([](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.begin();
}

void loop() {
  ws.cleanupClients();
  dnsServer.processNextRequest();
  setMotorSpeed(targetSpeedLeft, targetSpeedRight);

  unsigned long now = millis();
  if (hazardActive && strobeActive) {
      if (now - hazardLastToggle >= 150) { hazardLastToggle = now; hazardState = !hazardState; digitalWrite(PIN_HAZARD1, hazardState ? LOW : HIGH); digitalWrite(PIN_HAZARD2, hazardState ? HIGH : LOW); }
  } 
  else if (!hazardActive && strobeActive) {
      const int times[] = {60, 90, 60, 250, 60, 90, 60, 500};
      if (now - hazardLastToggle >= times[hazardPatternStep]) {
          hazardLastToggle = now; hazardPatternStep = (hazardPatternStep + 1) % 8;
          digitalWrite(PIN_HAZARD1, (hazardPatternStep == 0 || hazardPatternStep == 2) ? LOW : HIGH);
          digitalWrite(PIN_HAZARD2, (hazardPatternStep == 4 || hazardPatternStep == 6) ? LOW : HIGH);
      }
  }
  else if (hazardActive && !strobeActive) {
      if (now - hazardLastToggle >= HAZARD_INTERVAL_DEFAULT) { hazardLastToggle = now; hazardState = !hazardState; digitalWrite(PIN_HAZARD1, hazardState ? LOW : HIGH); digitalWrite(PIN_HAZARD2, hazardState ? LOW : HIGH); }
  }
  else { digitalWrite(PIN_HAZARD1, HIGH); digitalWrite(PIN_HAZARD2, HIGH); }
}