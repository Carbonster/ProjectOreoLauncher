// ui_v3.cpp - the launcher window, third take: a plain system window hosting WebView2.
//
// Two earlier takes (deleted before v1) drew everything with Dear ImGui. Rendering was fine, but every
// click had to survive our own hit testing, our own drag handling and ImGui's per-frame input
// state - and it did not. Here the UI is an HTML page inside Edge's WebView2 control: buttons
// are real HTML buttons, clicks are handled by the browser, and nothing about it can be broken
// by our frame loop, because there is no frame loop.
//
// The split:
//   C++  - owns the window and every service (python, pip, versions, loader, PLAY). Sends the
//          current state to the page as JSON, receives "the user pressed X" messages back.
//   HTML - draws. It never decides anything; it renders the JSON it is given.
//
// The page is compiled into this file (kUiHtml) so the launcher stays a single executable.
#include "ui.h"

#include <windows.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <wrl/event.h>        // Microsoft::WRL::Callback - the COM handlers WebView2 expects
#include <wrl/implements.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "../core/approved_catalog.h"
#include "../core/log.h"
#include "../core/process.h"
#include "../core/settings_service.h"
#include "../core/util.h"
#include "WebView2.h"

#pragma comment(lib, "WebView2LoaderStatic.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace oreo {

namespace {

// ---------------------------------------------------------------------------------------
// The page. Plain HTML/CSS/JS - no frameworks, no external files, no network.
// It renders whatever `render(state)` receives and posts back {action, id} on clicks.
// ---------------------------------------------------------------------------------------
const char* kUiHtml = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<style>
  :root {
    --bg:#17191b; --card:#212427; --border:#2e3236; --text:#f5f7f9; --dim:#d2d8de;
    --faint:#a8b1ba; --accent:#2f6fd0; --accent-hover:#3b82f6; --green:#3fb950;
    --amber:#d29922; --red:#f85149; --chip:#1c2b45; --chip-text:#8ab4f8;
  }
  * { box-sizing:border-box; }
  /* Tells the browser this is a dark page: scrollbars, form controls and focus rings all
     follow it, so the scrollbar stops being a white stripe down a black window. */
  html { color-scheme: dark; }
  html, body { height:100%; }
  body {
    margin:0; padding:22px 26px; background:var(--bg); color:var(--text);
    font:15px "Segoe UI",system-ui,sans-serif; user-select:none;
    /* PLAY and the status bar must never be a scroll away. The page is a column: only the
       mod list in the middle scrolls, everything else stays put however many mods there are. */
    display:flex; flex-direction:column; overflow:hidden;
  }
  #scroll { flex:1 1 auto; overflow-y:auto; min-height:0; margin-right:-8px; padding-right:8px; }
  .label { font-size:12px; letter-spacing:.08em; color:var(--dim); text-transform:uppercase; }
  .topline { display:flex; justify-content:space-between; align-items:center; gap:8px;
             font-size:13px; color:var(--dim); margin-bottom:14px; }
  .card { background:var(--card); border:1px solid var(--border); border-radius:10px;
          padding:16px 18px; margin:8px 0 16px; }
  .mod { display:flex; align-items:center; gap:18px; }
  .mod-requirements { border-top:1px solid var(--border); margin-top:16px; padding-top:14px; }
  .thumb { width:150px; height:86px; border-radius:8px; background:#1a1d1f;
           border:1px solid var(--border); display:flex; align-items:center;
           justify-content:center; color:var(--faint); font-size:30px; flex:0 0 auto; }
  .mod h2 { margin:0 0 8px; font-size:24px; font-weight:600; }
  .chips { display:flex; align-items:center; gap:10px; font-size:12px; }
  .chip { background:var(--chip); color:var(--chip-text); padding:3px 9px; border-radius:5px; }
  .grow { flex:1 1 auto; }
  .note { color:var(--faint); font-size:12px; margin-top:8px; }
  .description { color:var(--dim); font-size:14px; margin-top:10px; line-height:1.4; }
  /* The subtitle of a requirement explains what the mod needs it for, so it is worth reading
     and worth clicking. Dotted underline: a hint, not a navigation link. */
  .reqlink { cursor:pointer; border-bottom:1px dotted var(--dim); }
  .reqlink:hover { color:var(--text); border-bottom-color:var(--text); }
  body.hc .reqlink { border-bottom-color:#fff; }
  .switch { display:flex; align-items:center; gap:8px; color:var(--dim); cursor:pointer; }
  .switch input { width:18px; height:18px; accent-color:var(--accent); cursor:pointer; }
  table { width:100%; border-collapse:collapse; }
  /* Columns need real space between them: the mod name and its subtitle used to run straight
     into the INSTALLED column. The badge hugs the name, and the last column hugs the edge. */
  td { padding:11px 18px; vertical-align:middle; }
  td:first-child { padding-left:0; padding-right:4px; }
  td:last-child { padding-right:0; }
  tr + tr td { border-top:1px solid #24272a; }
  .badge { width:34px; height:34px; border-radius:9px; display:flex; align-items:center;
           justify-content:center; font-size:12px; font-weight:600; }
  .name { font-size:16px; }
  .sub { color:var(--faint); font-size:12px; }
  .cap { color:var(--faint); font-size:11px; text-transform:uppercase; letter-spacing:.05em; }
  .val { font-size:14px; }
  .status { font-size:14px; }
  .ok { color:var(--green); } .warn { color:var(--amber); } .bad { color:var(--red); }
  /* Busy state. A static row cannot tell you whether pip is still working or has wedged,
     so the dots run. CSS keeps them moving on its own - the page is only re-rendered when the
     state JSON actually changes, which it does not while a download is in progress. */
  .dots { display:inline-block; }
  .dots i { display:inline-block; width:4px; height:4px; border-radius:50%; margin-right:4px;
            background:currentColor; opacity:.25; animation:dotpulse 1.05s infinite; }
  .dots i:nth-child(2) { animation-delay:.18s; }
  .dots i:nth-child(3) { animation-delay:.36s; }
  @keyframes dotpulse { 0%,60%,100% { opacity:.25; } 30% { opacity:1; } }
  @media (prefers-reduced-motion: reduce) {
    .dots i { animation:none; opacity:.7; }
  }
  .muted { color:var(--faint); }
  button { font:inherit; border-radius:6px; border:1px solid transparent; cursor:pointer;
           padding:9px 16px; white-space:nowrap; }
  button:disabled { cursor:default; }
  .primary { background:var(--accent); color:#fff; }
  .primary:hover:enabled { background:var(--accent-hover); }
  .primary:disabled { background:#22252a; color:#666e75; }
  .ghost { background:#26292e; color:var(--text); border-color:#31353a; }
  .ghost:hover:enabled { background:#31353a; }
  .ghost:disabled { background:#1e2124; color:#5f666d; }
  .actioncell { text-align:right; white-space:nowrap; width:150px; }
  .info { background:#14222e; border:1px solid #1f406b; border-radius:8px; padding:12px 14px;
          color:var(--dim); font-size:13px; margin-top:6px; }
  .play { width:100%; padding:20px; font-size:20px; letter-spacing:.06em; margin-top:6px; }
  .footer { display:flex; align-items:center; gap:12px; margin:14px 0 4px;
            font-size:12px; color:var(--faint); }
  .statusbar { display:flex; justify-content:space-between; align-items:center;
               border-top:1px solid var(--border); margin-top:16px; padding-top:10px;
               font-size:12px; color:var(--faint); }
  .statusbar a { color:var(--dim); text-decoration:none; margin-right:16px; cursor:pointer; }
  .statusbar a:hover { color:#fff; text-decoration:underline; }
  /* The launcher's own update lives in the top right corner, where nothing else competes for
     attention. A filled pill when there is something to press; quiet text when there is not,
     because "up to date" and "update check failed" are statements, not buttons. */
  .update-link { background:var(--accent); color:#fff !important; font-weight:600;
                 padding:5px 13px; border-radius:999px; cursor:pointer; white-space:nowrap;
                 text-decoration:none !important; }
  .update-link:hover { background:var(--accent-hover); }
  .update-note { color:var(--faint) !important; cursor:default; white-space:nowrap;
                 text-decoration:none !important; }
  body.hc .update-link { background:#000; color:#4dff69 !important; border:2px solid #4dff69; }
  .dot { display:inline-block; width:8px; height:8px; border-radius:50%; margin-left:8px; }
  .errline { color:var(--dim); font-size:12px; padding:0 6px 10px; }
  .errline a { color:var(--chip-text); cursor:pointer; }
  pre { white-space:pre-wrap; word-break:break-all; background:#111315; padding:10px;
        border-radius:6px; max-height:280px; overflow:auto; font-size:12px; }
  dialog { background:var(--card); color:var(--text); border:1px solid var(--border);
           border-radius:10px; max-width:820px; width:90%; }
  /* An explanation of one component is a note, not a document - it should not span the window. */
  dialog.narrow { max-width:440px; }
  dialog.narrow h3 { font-size:17px; }
  dialog::backdrop { background:rgba(0,0,0,.55); }
  dialog h3 { margin:0 0 6px; font-size:20px; }

  /* Settings. Same palette and spacing as the rest of the page - it is another card, not
     another design. */
  .setgroup { margin:20px 0; }
  .setgroup > .label { margin-bottom:8px; }
  .pathrow { display:flex; gap:8px; align-items:center; }
  .pathrow input[type=text] { flex:1 1 auto; min-width:0; font:inherit; padding:8px 10px;
      background:#17191b; color:var(--text); border:1px solid var(--border); border-radius:6px; }
  .pathrow input[type=text]:focus { outline:none; border-color:var(--accent); }
  .fielderr { color:var(--red); font-size:12px; margin-top:6px; }
  .optionlist { display:flex; flex-direction:column; gap:11px; }
  .settingsfoot { display:flex; align-items:center; gap:10px; margin-top:22px;
      border-top:1px solid var(--border); padding-top:14px; }
  .settingsfoot a { color:var(--dim); text-decoration:none; cursor:pointer; font-size:12px; }
  .settingsfoot a:hover { color:#fff; text-decoration:underline; }

  /* Accessibility mode: maximum contrast, larger text, visible borders everywhere.
     Same layout, nothing hidden - only the palette and sizes change. */
  body.hc { background:#000; color:#fff; font-size:17px; }
  body.hc .card { background:#000; border:2px solid #fff; }
  body.hc .label, body.hc .cap { color:#fff; }
  body.hc .sub, body.hc .note, body.hc .muted, body.hc .footer, body.hc .statusbar { color:#fff; }
  body.hc .ok { color:#4dff69; } body.hc .warn { color:#ffd400; } body.hc .bad { color:#ff5f56; }
  body.hc .primary { background:#0060ff; color:#fff; border:2px solid #fff; }
  body.hc .ghost { background:#000; color:#fff; border:2px solid #fff; }
  body.hc button:disabled { color:#bdbdbd; border-color:#bdbdbd; }
  body.hc .chip { background:#000; color:#fff; border:2px solid #fff; }
  body.hc .info { background:#000; border:2px solid #fff; color:#fff; }
  body.hc .thumb { border:2px solid #fff; }
  body.hc .mod-requirements { border-top:2px solid #fff; }
  body.hc tr + tr td { border-top:2px solid #fff; }
  body.hc .statusbar a { color:#fff; text-decoration:underline; }
  body.hc .name, body.hc .val, body.hc .status { font-size:18px; }
  body.hc dialog { background:#000; color:#fff; border:2px solid #fff; }
  body.hc .pathrow input[type=text] { background:#000; color:#fff; border:2px solid #fff; }
  body.hc .settingsfoot { border-top:2px solid #fff; }
  body.hc .settingsfoot a { color:#fff; text-decoration:underline; }
  body.hc .fielderr { color:#ff5f56; }
</style></head>
<body>
  <div class="topline">
    <span id="gameversion">No Man's Sky</span>
    <a id="launcherupdate" class="update-link" onclick="send('updatelauncher')"
        style="display:none"></a>
  </div>
  <div id="scroll">
    <div class="label" id="modcount">DETECTED MODS</div>
    <div id="mods"></div>
    <div class="footer">
      <button class="ghost" onclick="send('logs')">Open Logs</button>
    </div>
  </div>
  <button class="primary play" id="play" onclick="send('play')">PLAY</button>
  <div id="playstage" class="note" style="font-size:14px"></div>
  <div id="playmsg" class="note"></div>
  <div class="statusbar">
    <span id="version"></span>
    <span>
      <a onclick="send('settings')">Settings</a>
      <a id="rechecklink" onclick="send('recheck')">Check Again</a>
      <a onclick="send('contrast')" id="contrastlink">High contrast</a>
      <span id="debughotkeys"></span>
    </span>
    <span><span id="python"></span><span class="dot" id="pydot"></span></span>
  </div>

  <dialog id="dlg"><h3 id="dlgtitle"></h3><div id="dlgbody"></div>
    <div style="margin-top:14px;text-align:right">
      <button class="ghost" onclick="document.getElementById('dlg').close()">Close</button>
    </div>
  </dialog>

  <dialog id="settingsdlg">
    <h3>Settings</h3>
    <div class="note">Saved to config.ini. You do not need to edit that file by hand.</div>

    <div class="setgroup">
      <div class="label">Game folder</div>
      <div class="pathrow">
        <input type="text" id="setGamePath" spellcheck="false" placeholder="No game folder detected">
        <button class="ghost" onclick="send('browsegame')">Browse...</button>
        <button class="ghost" onclick="useAutoGame()">Use auto-detected path</button>
      </div>
      <div class="note" id="setGameAuto"></div>
      <div class="fielderr" id="setGameErr"></div>
    </div>

    <div class="setgroup">
      <div class="label">Mods folder</div>
      <div class="pathrow">
        <input type="text" id="setModsPath" spellcheck="false" placeholder="No mods folder yet">
        <button class="ghost" onclick="send('browsemods')">Browse...</button>
        <button class="ghost" onclick="useDefaultMods()">Use default folder</button>
      </div>
      <div class="note" id="setModsAuto"></div>
      <div class="fielderr" id="setModsErr"></div>
    </div>

    <div class="setgroup">
      <div class="label">Launcher</div>
      <div class="optionlist">
        <label class="switch"><input type="checkbox" id="setAlwaysShow"
            onchange="syncAlwaysShowNote()"> Always show Launcher</label>
        <div class="note" id="alwaysshownote" style="margin:-4px 0 2px 28px;display:none">
          Off: the Launcher only opens when something needs you - an update, or a component
          that is missing or failed. Otherwise the game starts straight away.</div>
        <label class="switch"><input type="checkbox" id="setCheckUpdates">
          Check for updates</label>
        <label class="switch"><input type="checkbox" id="setHighContrast">
          High contrast</label>
        <label class="switch"><input type="checkbox" id="setDebugHotkeys">
          Debug hotkeys</label>
        <label class="switch"><input type="checkbox" id="setVerbose">
          Verbose logging</label>
      </div>
    </div>

    <div class="fielderr" id="setGeneralErr"></div>
    <div class="settingsfoot">
      <a onclick="send('openconfig')">Open config.ini</a>
      <span class="grow"></span>
      <button class="ghost" onclick="restoreDefaults()">Restore defaults</button>
      <button class="ghost" onclick="closeSettings()">Cancel</button>
      <button class="primary" onclick="saveSettings()">Save</button>
    </div>
  </dialog>

<script>
  function send(action, id) {
    window.chrome.webview.postMessage(JSON.stringify({action: action, id: id || ""}));
  }
  // The Settings form carries more than one value, so it posts a whole object. Every field is
  // a string: the C++ side reads this JSON with a deliberately tiny parser.
  function sendObj(message) { window.chrome.webview.postMessage(JSON.stringify(message)); }
  const esc = s => (s == null ? "" : String(s).replace(/[&<>"]/g,
      c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])));
  const ICON = {ok:"✔", warn:"⚠", bad:"✖", busy:"⋯"};
  const DOTS = '<span class="dots"><i></i><i></i><i></i></span>';
  const BADGE = {pymhf:["#1f3a5c","#9dc6ff","Py"],
                 nmspy:["#392b52","#c3a8ff","N"],
                 runtime:["#1e3b33","#8fd9c0","R"]};

  function requirementRows(components, busy) {
    return components.map(c => {
      const b = BADGE[c.id] || ["#33383d","#fff","?"];
      const cls = c.tone;
      const act = c.busy
        ? `<button class="ghost" disabled>${DOTS} ${esc(c.progress.replace(/\.+$/, ""))}</button>`
        : (c.action
            ? `<button class="primary"${busy ? " disabled" : ""} onclick="send('action','${c.id}')">${esc(c.action)}</button>`
            : `<button class="ghost" disabled>Up to date</button>`);
      const err = c.error
        ? `<tr><td colspan="5" class="errline">${esc(c.error)}
             ${c.details ? `<a onclick="send('details','${c.id}')">Details</a>` : ""}</td></tr>`
        : "";
      // While something is running the leading glyph becomes three moving dots, and the
      // trailing "..." in the message would just repeat them.
      const mark = cls === "busy" ? DOTS : (ICON[cls] || "");
      const text = cls === "busy" ? c.message.replace(/\.+$/, "") : c.message;
      return `<tr>
        <td style="width:44px"><div class="badge" style="background:${b[0]};color:${b[1]}">${b[2]}</div></td>
        <td><div class="name">${esc(c.name)}</div>
            <div class="sub"><span class="reqlink" onclick="send('about','${c.id}')"
                title="What is this and why does the mod need it?">${esc(c.subtitle)}</span></div></td>
        <td style="width:130px"><div class="cap">Installed</div>
            <div class="val ${c.installed ? '' : 'muted'}">${esc(c.installed || "not installed")}</div></td>
        <td><span class="status ${cls}">${mark} ${esc(text)}</span></td>
        <td class="actioncell">${act}</td></tr>${err}`;
    }).join("");
  }

  // Only mods the launcher actually manages get anything here. For those, the state is always
  // visible: "Up to date" is information too, and its absence reads as "nothing happened".
  function modUpdateButton(m, busy) {
    // The row being updated shows the same pulsing dots the components use, with whatever stage
    // the installer last reported. Every other Update button goes flat for the duration: only
    // one install can run at a time, and a click that quietly does nothing is worse than a
    // button that says it is not available right now.
    if (m.updating)
      return `<button class="ghost" disabled>${DOTS} ${esc((m.updateStage || "Updating").replace(/\.+$/, ""))}</button>`;
    if (m.update === "available")
      return `<button class="primary"${busy ? " disabled" : ""} onclick="send('updatemod','${m.index}')">Update</button>`;
    if (m.update === "current")
      return `<button class="ghost" disabled>Up to date</button>`;
    if (m.update === "unknown")
      return `<button class="ghost" disabled title="Could not reach the update server">Update: unknown</button>`;
    return "";
  }

  function render(state) {
    // One file-touching operation at a time, so while any of them runs every button that
    // would start another goes flat. The C++ side refuses them regardless - this is so the
    // player can see why, instead of clicking something that quietly does nothing.
    const busy = !!state.busyWith;

    document.getElementById('gameversion').textContent =
        "No Man's Sky: " + (state.gameVersion || "unknown");

    document.getElementById('modcount').textContent =
        "DETECTED MODS (" + state.mods.length + ")";

    document.getElementById('mods').innerHTML = state.mods.length ? state.mods.map(m => `
      <div class="card">
        <div class="mod">
          <div class="thumb">${m.thumb ? `<img src="${esc(m.thumb)}" style="width:100%;height:100%;object-fit:cover;border-radius:8px">` : esc(m.name.charAt(0))}</div>
          <div>
            <h2>${esc(m.name)}</h2>
            <div class="chips">
              <span class="chip">${esc(m.kind)}</span>
              ${state.debugHotkeys ? `<span class="chip">${m.version ? "v" + esc(m.version) : "version unknown"}</span>` : ""}
            </div>
            ${m.summary ? `<div class="description">${esc(m.summary)}</div>` : ""}
          </div>
          <span class="grow"></span>
          ${modUpdateButton(m, busy)}
          <button class="ghost" onclick="send('moddetails','${m.index}')">Details</button>
        </div>
        ${m.isPython ? `<div class="mod-requirements">
          <div class="label">Requirements</div>
          <table>${requirementRows(m.requirements, busy)}</table>
        </div>` : ""}
      </div>`).join("") : `<div class="card"><b>No ProjectOreo mods found</b>
        <div class="note">Looked in ${esc(state.modsPath)}</div></div>`;

    document.body.classList.toggle('hc', !!state.highContrast);
    document.getElementById('contrastlink').textContent =
        state.highContrast ? "Normal contrast" : "High contrast";
    document.getElementById('playstage').textContent = state.playStage || "";

    const play = document.getElementById('play');
    play.textContent = state.playRunning ? "STARTING..." : "PLAY";
    play.disabled = !!state.playRunning || busy;
    document.getElementById('playmsg').textContent = state.playMessage || "";
    document.getElementById('version').textContent = "ProjectOreo " + state.version;
    const lu = document.getElementById('launcherupdate');
    const known = {available: "Update to " + state.launcherLatest,
                   current: "up to date",
                   unknown: "update check failed"};
    lu.textContent = known[state.launcherUpdate] || "";
    lu.style.display = lu.textContent ? "inline-block" : "none";
    // Only the one worth clicking looks like a button; the rest are just words up there.
    // Updating the launcher replaces the running exe and hands the session over to it, so it
    // is the one thing that must never start on top of an install - and it was the only
    // button with no guard at all.
    const luLive = state.launcherUpdate === "available" && !busy;
    lu.className = luLive ? "update-link" : "update-note";
    lu.onclick = luLive ? () => send('updatelauncher') : null;
    lu.title = busy && state.launcherUpdate === "available"
        ? "Available once the launcher has finished " + state.busyWith : "";
    const recheck = document.getElementById('rechecklink');
    // Not a read-only link: it runs pip to read the installed versions.
    recheck.style.opacity = busy ? "0.45" : "";
    recheck.style.pointerEvents = busy ? "none" : "";
    document.getElementById('debughotkeys').textContent =
        state.debugHotkeys ? "DebugHotkeys: true" : "";
    document.getElementById('python').textContent =
        state.pythonVersion ? "Python: " + state.pythonVersion + " (bundled)" : "Python: missing";
    document.getElementById('pydot').style.background =
        state.pythonVersion ? "var(--green)" : "var(--red)";
  }

  function showDialog(title, html, narrow) {
    document.getElementById('dlgtitle').textContent = title;
    document.getElementById('dlgbody').innerHTML = html;
    const dlg = document.getElementById('dlg');
    dlg.classList.toggle('narrow', !!narrow);
    dlg.showModal();
  }

  // ---- Settings -------------------------------------------------------------------------
  // The form is filled from C++ when it opens and read back only when Save is pressed, so
  // Cancel genuinely changes nothing - there is no path that writes on edit.
  let settingsState = {};
  const el = id => document.getElementById(id);
  const PATH_FIELD = {game: 'setGamePath', mods: 'setModsPath'};
  const FLAGS = [['setAlwaysShow', 'alwaysShow'], ['setCheckUpdates', 'checkUpdates'],
                 ['setHighContrast', 'highContrast'], ['setDebugHotkeys', 'debugHotkeys'],
                 ['setVerbose', 'verbose']];

  // An empty override means "work it out yourself", but showing an empty box is unhelpful -
  // the user wants to see which folder that actually is. So the detected path goes straight
  // into the field, and saveSettings turns it back into an empty override if it was not edited.
  const samePath = (a, b) =>
      (a || "").trim().replace(/[\/]+$/, "").toLowerCase() ===
      (b || "").trim().replace(/[\/]+$/, "").toLowerCase();

  function syncAlwaysShowNote() {
    el('alwaysshownote').style.display = el('setAlwaysShow').checked ? "none" : "block";
  }

  function fillSettings(values) {
    el('setGamePath').value = values.gamePath || settingsState.autoGamePath || "";
    el('setModsPath').value = values.modsPath || settingsState.autoModsPath || "";
    FLAGS.forEach(([id, key]) => { el(id).checked = !!values[key]; });
    syncAlwaysShowNote();
  }

  function clearSettingsErrors() {
    el('setGameErr').textContent = "";
    el('setModsErr').textContent = "";
    el('setGeneralErr').textContent = "";
  }

  function showSettings(state) {
    settingsState = state;
    fillSettings(state.values);
    el('setGameAuto').textContent = state.autoGamePath
      ? (state.values.gamePath ? "Detected automatically: " + state.autoGamePath
                               : "This is the folder detection found.")
      : "No Man's Sky was not detected automatically - pick the folder yourself.";
    el('setModsAuto').textContent = state.autoModsPath
      ? (state.values.modsPath ? "Default folder: " + state.autoModsPath
                               : "This is the default folder.")
      : "The default folder is only known once the game folder is set.";
    clearSettingsErrors();
    el('settingsdlg').showModal();
  }

  // Called back by C++ after the Windows folder picker closes. An empty path means the user
  // cancelled the picker, and the box keeps whatever it had.
  function setSettingsPath(which, path) {
    if (path) el(PATH_FIELD[which]).value = path;
  }

  function settingsFailed(gameError, modsError, generalError) {
    el('setGameErr').textContent = gameError || "";
    el('setModsErr').textContent = modsError || "";
    el('setGeneralErr').textContent = generalError || "";
  }

  function closeSettings() { el('settingsdlg').close(); }

  function useAutoGame() {
    el('setGamePath').value = settingsState.autoGamePath || "";
    clearSettingsErrors();
  }
  function useDefaultMods() {
    el('setModsPath').value = settingsState.autoModsPath || "";
    clearSettingsErrors();
  }

  // Only fills the form. Nothing is written until Save, so this is undoable with Cancel.
  function restoreDefaults() {
    fillSettings(settingsState.defaults || {});
    clearSettingsErrors();
  }

  function saveSettings() {
    clearSettingsErrors();
    // A field still holding the detected path is not an override - send it back as empty, so
    // the launcher keeps detecting instead of freezing today's answer into config.ini.
    const game = el('setGamePath').value.trim();
    const mods = el('setModsPath').value.trim();
    const message = {action: 'savesettings',
                     gamePath: samePath(game, settingsState.autoGamePath) ? "" : game,
                     modsPath: samePath(mods, settingsState.autoModsPath) ? "" : mods};
    FLAGS.forEach(([id, key]) => { message[key] = el(id).checked ? "1" : "0"; });
    sendObj(message);
  }
</script></body></html>)HTML";

// ---------------------------------------------------------------------------------------
// Window + WebView2 plumbing
// ---------------------------------------------------------------------------------------
HWND g_hwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;
App* g_app = nullptr;
bool g_played = false;
bool g_closing = false;
std::string g_last_state_json;

std::string json_escape(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back((char)c);
                }
        }
    }
    return out;
}

std::string quoted(const std::string& value) { return "\"" + json_escape(value) + "\""; }

std::string html_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

// Maps a component state to the colour/icon the page uses. The page stays dumb on purpose.
const char* tone_for(const Component& c) {
    switch (c.state) {
        case CompState::Ready: return "ok";
        case CompState::Checking:
        case CompState::Installing:
        case CompState::Updating: return "busy";
        case CompState::NotInstalled:
        case CompState::CheckFailed:
        case CompState::InstallFailed:
        case CompState::UpdateFailed: return "bad";
        default: return "warn";
    }
}

std::string component_to_json(const Component& c) {
    std::string json = "{";
    json += "\"id\":" + quoted(c.id) + ",";
    json += "\"name\":" + quoted(c.name) + ",";
    json += "\"subtitle\":" + quoted(c.subtitle) + ",";
    json += "\"installed\":" + quoted(c.installed) + ",";
    json += "\"message\":" + quoted(c.busy ? c.progress : c.message) + ",";
    json += "\"error\":" + quoted(c.needs_attention() ? c.error : "") + ",";
    json += "\"details\":" + std::string(c.details.valid ? "true" : "false") + ",";
    json += "\"busy\":" + std::string(c.busy ? "true" : "false") + ",";
    json += "\"progress\":" + quoted(c.progress) + ",";
    json += "\"tone\":" + quoted(tone_for(c)) + ",";
    json += "\"action\":" + quoted(to_string(c.action));
    json += "}";
    return json;
}

// Which components appear in a MOD'S requirements block - the mod's, because the answer is not
// the same for every mod.
//
// NMSpy is on every Python mod: it is what loads them, so no Python mod runs without it.
//
// The Runtime is not. It provides an overlay and controller reading, and a mod that only changes
// game logic never touches either - the NMSpy example mods do not. Listing it for everyone told
// players that a bare example needed a component it has no use for, and offered them an Install
// button to prove it. So the mod says: `Runtime=` under `[Compatibility]` in its manifest. A mod
// without a manifest has said nothing, and nothing is not a requirement.
//
// pyMHF depends on who is looking. Nobody installs it on purpose: NMSpy depends on it, so
// installing NMSpy brings the right pyMHF along, and pip decides which one that is. Showing a
// player its own row with its own Install button only ever asked a question they had no way to
// answer, so for a player it has none. Everything about it still works - it is detected, its
// version is read, its absence still stops PLAY and is written to the log.
//
// With DebugHotkeys the row comes back, because the question changes. Which pyMHF pip actually
// resolved is the first thing worth knowing when a mod fails to load: 0.2.4 removed
// `pymhf/utils/partial_struct.py`, which NMSpy 170671.3 imports, and the symptom was a mod that
// simply did not appear. Reading that version out of a log is a detour when the launcher is
// already on screen with a row-shaped hole where it belongs.
bool shown_in_requirements(const std::string& component_id, const ModInfo& mod, bool debug) {
    if (component_id == "nmspy") return true;
    if (component_id == "runtime") return mod.needs_runtime();
    if (component_id == "pymhf") return debug;
    return false;
}

// Whether any mod present asks for this component at all. Used to decide whether a component in
// trouble is worth shouting about: a broken Runtime matters only if something needs it.
bool wanted_by_any_mod(const std::string& component_id, const std::vector<ModInfo>& mods,
                       bool debug) {
    for (const ModInfo& m : mods) {
        if (m.is_python && shown_in_requirements(component_id, m, debug)) return true;
    }
    return false;
}

// The page decides what to draw from this, never from a boolean: "no update" and "could not
// ask" look the same to a flag and completely different to a person.
const char* update_state_name(AppSnapshot::UpdateState state) {
    switch (state) {
        case AppSnapshot::UpdateState::Unknown: return "unknown";
        case AppSnapshot::UpdateState::Current: return "current";
        case AppSnapshot::UpdateState::Available: return "available";
        default: return "unmanaged";
    }
}

std::string state_to_json(const AppSnapshot& snap, const Config& cfg) {
    std::string json = "{";
    json += "\"checking\":" + std::string(snap.checking ? "true" : "false") + ",";
    json += "\"checkStatus\":" + quoted(snap.checking ? "Checking for updates..." : snap.check_status) + ",";
    json += "\"playRunning\":" + std::string(snap.play_running || snap.play_done ? "true" : "false") + ",";
    json += "\"playMessage\":" + quoted(snap.play_message) + ",";
    json += "\"playStage\":" + quoted(snap.play_stage) + ",";
    // What the whole page greys itself out on: empty when idle, otherwise a human
    // fragment ("updating NMSpy") that the disabled buttons quote back.
    json += "\"busyWith\":" + quoted(snap.busy_with) + ",";
    json += "\"highContrast\":" + std::string(snap.high_contrast ? "true" : "false") + ",";
    json += "\"version\":" + quoted(PROJECTOREO_VERSION) + ",";
    json += "\"debugHotkeys\":" + std::string(cfg.debug_hotkeys ? "true" : "false") + ",";
    json += "\"gameVersion\":" + quoted(snap.game_version) + ",";
    json += "\"pythonVersion\":" + quoted(snap.python_version) + ",";
    json += "\"modsPath\":" + quoted(snap.mods_path) + ",";
    json += "\"launcherUpdate\":" + quoted(update_state_name(snap.launcher_update)) + ",";
    json += "\"launcherLatest\":" + quoted(snap.launcher_latest) + ",";

    bool active_python_requirements = false;
    for (const Component& c : snap.components) {
        if (wanted_by_any_mod(c.id, snap.mods, cfg.debug_hotkeys) && (c.needs_attention() || c.busy)) {
            active_python_requirements = true;
            break;
        }
    }
    std::vector<size_t> mod_order(snap.mods.size());
    for (size_t i = 0; i < mod_order.size(); ++i) mod_order[i] = i;
    auto group = [active_python_requirements, &snap](size_t index) {
        const ModInfo& mod = snap.mods[index];
        if (mod.is_python && active_python_requirements) return 0;
        if (mod.is_python) return 1;
        return 2;
    };
    std::stable_sort(mod_order.begin(), mod_order.end(), [&](size_t a, size_t b) {
        int group_a = group(a), group_b = group(b);
        if (group_a != group_b) return group_a < group_b;
        return to_lower(snap.mods[a].name) < to_lower(snap.mods[b].name);
    });

    json += "\"mods\":[";
    bool first_mod = true;
    for (size_t index : mod_order) {
        const ModInfo& m = snap.mods[index];
        if (!first_mod) json += ",";
        first_mod = false;
        json += "{";
        json += "\"index\":" + std::to_string(index) + ",";
        json += "\"name\":" + quoted(m.name) + ",";
        json += "\"description\":" + quoted(m.description) + ",";
        json += "\"summary\":" + quoted(m.summary) + ",";
        json += "\"version\":" + quoted(m.version) + ",";
        json += "\"enabled\":" + std::string(m.enabled ? "true" : "false") + ",";
        auto mod_state = snap.mod_updates.find(index);
        json += "\"update\":" +
                quoted(update_state_name(mod_state == snap.mod_updates.end()
                                             ? AppSnapshot::UpdateState::Unmanaged
                                             : mod_state->second)) + ",";
        const bool updating = !snap.mod_update_folder.empty() && m.folder == snap.mod_update_folder;
        json += "\"updating\":" + std::string(updating ? "true" : "false") + ",";
        json += "\"updateStage\":" + quoted(updating ? snap.mod_update_stage : std::string()) + ",";
        json += "\"isPython\":" + std::string(m.is_python ? "true" : "false") + ",";
        json += "\"requirementsActive\":" +
                std::string(m.is_python && active_python_requirements ? "true" : "false") + ",";
        json += "\"requirements\":[";
        bool first_requirement = true;
        if (m.is_python) {
            for (const Component& c : snap.components) {
                if (!shown_in_requirements(c.id, m, cfg.debug_hotkeys)) continue;
                if (!first_requirement) json += ",";
                first_requirement = false;
                json += component_to_json(c);
            }
        }
        json += "],";
        json += "\"kind\":" + quoted(m.is_python ? "Python mod" : "Mod") + ",";
        // The page cannot open local files, so the thumbnail travels inside the JSON as a
        // data URI. Mod art is small; anything unreasonable is skipped rather than inlined.
        std::string thumb;
        if (!m.thumbnail.empty()) {
            std::string file = path_join(m.path, m.thumbnail);
            std::string ext = path_ext(file);
            const char* mime = ext == ".jpg" || ext == ".jpeg" ? "image/jpeg"
                               : ext == ".gif"                 ? "image/gif"
                                                               : "image/png";
            std::string data;
            HANDLE probe = CreateFileW(to_wide(file).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            LARGE_INTEGER size{};
            bool small_enough = probe != INVALID_HANDLE_VALUE && GetFileSizeEx(probe, &size) &&
                                size.QuadPart <= 4 * 1024 * 1024;
            if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
            if (small_enough) data = read_file_base64(file);
            if (!data.empty()) thumb = std::string("data:") + mime + ";base64," + data;
            else Log::warn("thumbnail skipped (missing or larger than 4 MB): " + file);
        }
        json += "\"thumb\":" + quoted(thumb);
        json += "}";
    }
    json += "]}";
    return json;
}

void push_state() {
    if (!g_webview || !g_app) return;
    std::string json = state_to_json(g_app->snapshot(), g_app->config());
    if (json == g_last_state_json) return;   // nothing changed: leave the page alone
    g_last_state_json = json;
    std::wstring script = L"render(" + to_wide(json) + L");";
    g_webview->ExecuteScript(script.c_str(), nullptr);
}

// Runs one statement in the page. Everything C++ tells the UI goes through here.
void call_page(const std::string& javascript) {
    if (!g_webview) return;
    g_webview->ExecuteScript(to_wide(javascript).c_str(), nullptr);
}

// The standard Windows folder picker. Returns "" when the user cancels, which the page treats
// as "keep what was in the box".
std::string pick_folder(HWND owner, const wchar_t* title, const std::string& initial) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        Log::error("the Windows folder picker could not be created");
        return "";
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    // FORCEFILESYSTEM keeps the result a real path: no Libraries, no virtual shell folders.
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);
    if (!initial.empty()) {
        ComPtr<IShellItem> start;
        if (SUCCEEDED(SHCreateItemFromParsingName(to_wide(initial).c_str(), nullptr,
                                                  IID_PPV_ARGS(&start)))) {
            dialog->SetFolder(start.Get());
        }
    }
    if (FAILED(dialog->Show(owner))) return "";   // cancelled: not an error

    ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result))) return "";
    LPWSTR path = nullptr;
    std::string picked;
    if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        picked = to_utf8(path);
        CoTaskMemFree(path);
    }
    return picked;
}

std::string settings_values_json(const SettingsValues& values) {
    std::string json = "{";
    json += "\"gamePath\":" + quoted(values.game_path) + ",";
    json += "\"modsPath\":" + quoted(values.mods_path) + ",";
    json += "\"alwaysShow\":" + std::string(values.always_show_window ? "true" : "false") + ",";
    json += "\"checkUpdates\":" + std::string(values.check_for_updates ? "true" : "false") + ",";
    json += "\"highContrast\":" + std::string(values.high_contrast ? "true" : "false") + ",";
    json += "\"debugHotkeys\":" + std::string(values.debug_hotkeys ? "true" : "false") + ",";
    json += "\"verbose\":" + std::string(values.verbose ? "true" : "false");
    json += "}";
    return json;
}

// Current values plus the two paths detection came up with, so the window can show what
// "automatic" resolved to instead of an empty box.
std::string settings_state_json(App& app) {
    std::string json = "{";
    json += "\"values\":" + settings_values_json(app.settings_values()) + ",";
    json += "\"defaults\":" + settings_values_json(SettingsService::defaults()) + ",";
    json += "\"autoGamePath\":" + quoted(app.detected_game_path()) + ",";
    json += "\"autoModsPath\":" + quoted(app.detected_mods_path());
    json += "}";
    return json;
}

void show_dialog(const std::string& title, const std::string& html, bool narrow = false) {
    if (!g_webview) return;
    std::wstring script = L"showDialog(" + to_wide(quoted(title)) + L", " + to_wide(quoted(html)) +
                          (narrow ? L", true" : L", false") + L");";
    g_webview->ExecuteScript(script.c_str(), nullptr);
}

// Why a mod needs this component, in the terms a player cares about: what it does to their
// game, not what it is made of. One paragraph each; the links do the rest.
const char* why_needed(const std::string& id) {
    if (id == "pymhf")
        return "A Python mod is not a game file - it is a program that has to run inside No Man's "
               "Sky while you play. pyMHF is what puts it there and lets it read and change what "
               "the game is doing, moment to moment.";
    if (id == "nmspy")
        return "pyMHF knows how to run Python inside a game, but not which game. NMSpy is the part "
               "that speaks No Man's Sky specifically: it knows where things like ships, fish and "
               "the player live in memory, so a mod can work with them by name instead of by "
               "address. It is also what keeps mods working after a game update.";
    if (id == "runtime")
        return "Some mods need more than game logic: their own interface drawn on top of the game, "
               "and a controller they can read while the game reads it too. Project Oreo Runtime "
               "provides both, so every mod does not have to build them again.";
    if (id == "loader")
        return "The loader is a small file next to the game. The game loads it on its own at "
               "startup, which is what lets ProjectOreo start with your game and not need a "
               "separate shortcut or Steam launch option.";
    return "";
}

// GitHub URLs are https://github.com/<owner>/<repo> - the owner is the author, so there is no
// second field to keep in step with the first.
std::string repo_owner(const std::string& url) {
    const std::string marker = "github.com/";
    size_t start = url.find(marker);
    if (start == std::string::npos) return "";
    start += marker.size();
    size_t end = url.find('/', start);
    return end == std::string::npos ? url.substr(start) : url.substr(start, end - start);
}

// Pulls a string field out of the tiny JSON the page posts back.
std::string message_field(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";
    std::string out;
    for (size_t i = pos + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            char n = json[++i];
            out.push_back(n == 'n' ? '\n' : n);
            continue;
        }
        if (json[i] == '"') break;
        out.push_back(json[i]);
    }
    return out;
}

void handle_message(const std::string& json) {
    std::string action = message_field(json, "action");
    std::string id = message_field(json, "id");
    Log::debug("ui action: " + action + " " + id);

    AppSnapshot snap = g_app->snapshot();
    if (action == "play") {
        g_played = true;
        g_app->play_async();
    } else if (action == "action") {
        g_app->run_action(id);
    } else if (action == "contrast") {
        g_app->toggle_high_contrast();
    } else if (action == "recheck") {
        g_app->recheck_async();
    } else if (action == "logs") {
        shell_open(path_dir(snap.log_path));
    } else if (action == "settings") {
        call_page("showSettings(" + settings_state_json(*g_app) + ");");
    } else if (action == "openconfig") {
        // Kept as a developer escape hatch. Everything in that file is in the window above it.
        shell_open(g_app->config().config_path);
    } else if (action == "browsegame" || action == "browsemods") {
        const bool game = action == "browsegame";
        std::string initial = game ? snap.game_path : snap.mods_path;
        std::string picked = pick_folder(g_hwnd,
                                         game ? L"Select the No Man's Sky folder"
                                              : L"Select the mods folder",
                                         initial);
        call_page("setSettingsPath(" + quoted(game ? "game" : "mods") + ", " + quoted(picked) + ");");
    } else if (action == "savesettings") {
        SettingsValues values;
        values.game_path = message_field(json, "gamePath");
        values.mods_path = message_field(json, "modsPath");
        values.always_show_window = message_field(json, "alwaysShow") == "1";
        values.check_for_updates = message_field(json, "checkUpdates") == "1";
        values.high_contrast = message_field(json, "highContrast") == "1";
        values.debug_hotkeys = message_field(json, "debugHotkeys") == "1";
        values.verbose = message_field(json, "verbose") == "1";

        SettingsErrors errors;
        std::string error;
        if (g_app->apply_settings(values, errors, error)) {
            // The mod list, the paths and the theme may all have changed: force a full repaint.
            g_last_state_json.clear();
            call_page("closeSettings();");
        } else {
            call_page("settingsFailed(" + quoted(errors.game_path) + ", " + quoted(errors.mods_path) +
                      ", " + quoted(error) + ");");
        }
    } else if (action == "updatemod") {
        g_app->update_mod_async((size_t)atoi(id.c_str()));
    } else if (action == "updatelauncher") {
        g_app->update_launcher_async();
    } else if (action == "openmodfolder") {
        size_t index = (size_t)atoi(id.c_str());
        if (index < snap.mods.size()) shell_open(snap.mods[index].path);
    } else if (action == "moddetails") {
        // The page sends an index, not a path: Windows paths are full of backslashes, which a
        // JavaScript string literal would swallow as escape sequences.
        size_t index = (size_t)atoi(id.c_str());
        if (index < snap.mods.size()) {
            const ModInfo& m = snap.mods[index];
            auto row = [](const char* caption, const std::string& value) {
                return "<div style='margin:6px 0'><span style='color:#a8b1ba'>" + std::string(caption) +
                       ":</span> " + (value.empty() ? "unknown" : html_escape(value)) + "</div>";
            };
            std::string body;
            if (!m.description.empty())
                body += "<div style='line-height:1.5;margin-bottom:14px'>" +
                        html_escape(m.description) + "</div>";
            if (g_app->config().debug_hotkeys) {
                body += row("Version", m.version) + row("Entry point", m.entry_point) +
                        row("Metadata file", m.has_manifest ? m.manifest_path : "");
            }
            body += "<div style='margin-top:16px'><button class='ghost' onclick=\"send('openmodfolder','" +
                    std::to_string(index) + "')\">Open Mod Folder</button></div>";
            show_dialog(m.name, body);
        }
    } else if (action == "openurl") {
        // Anything the page asks to open goes through the browser, and only if it is a plain
        // https GitHub address. The page is ours, but "the page is ours" is not a check.
        const std::string prefix = "https://github.com/";
        bool safe = id.compare(0, prefix.size(), prefix) == 0 &&
                    id.find_first_of(" \"'<>\r\n\t") == std::string::npos;
        if (safe) shell_open(id);
        else Log::warn("refused to open a link that is not a plain GitHub address: " + id);
    } else if (action == "about") {
        // What this component is, who wrote it and why the mod cannot do without it. Deliberately
        // a note and not a page: it opens from a subtitle, and it should read like an answer.
        for (const Component& c : snap.components) {
            if (c.id != id) continue;
            const ApprovedCatalogEntry* entry = find_approved(c.id);
            std::string body = "<div style='line-height:1.5'>" + std::string(why_needed(c.id)) +
                               "</div>";
            if (entry && !entry->repository_url.empty()) {
                std::string owner = repo_owner(entry->repository_url);
                auto link = [](const std::string& url, const std::string& text) {
                    return "<a onclick=\"send('openurl','" + html_escape(url) + "')\">" +
                           html_escape(text) + "</a>";
                };
                body += "<div style='margin-top:14px;font-size:13px;color:#a8b1ba'>";
                if (!owner.empty())
                    body += "By " + link("https://github.com/" + owner, owner) + "<br>";
                body += link(entry->repository_url, entry->repository_url) + "</div>";
            }
            show_dialog(c.name, body, true);
            break;
        }
    } else if (action == "details") {
        for (const Component& c : snap.components) {
            if (c.id != id) continue;
            const OperationDetails& d = c.details;
            std::string body = "<div style='color:#d2d8de'>" + c.error + "</div><pre>" +
                               "command: " + d.command + "\nexit code: " + d.exit_code +
                               "\npython: " + d.python_path + "\nversions: " + d.package_version +
                               "\ntime: " + d.timestamp + "\n\n" + d.out + "\n" + d.err + "</pre>";
            show_dialog(c.name, body);
            break;
        }
    }
    push_state();
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE:
            if (g_controller) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                g_controller->put_Bounds(rc);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = (MINMAXINFO*)lparam;
            info->ptMinTrackSize.x = 900;
            info->ptMinTrackSize.y = 700;
            return 0;
        }
        case WM_TIMER: {
            push_state();
            // The window stays up through the whole launch - releasing the game, waiting for
            // its window, injecting the mod - and only leaves once that is done, plus a short
            // beat so the last line ("... is running") is actually readable.
            // The launcher replaced itself: nothing more to show, main() starts the new one.
            // Never while an operation is still running, though. The gate means that can no
            // longer happen, but closing the window here is precisely what used to turn an
            // overlap into an invisible one - the process stayed alive finishing pip with no
            // window and, by then, no log. Waiting a tick longer costs nothing: the update
            // worker still holds the gate for the instant after it sets the restart path, so
            // the close simply happens on the next timer.
            if (g_app && !g_app->pending_restart().empty() &&
                g_app->snapshot().busy_with.empty()) {
                g_closing = true;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            static int done_ticks = 0;
            if (g_app && g_app->play_finished()) {
                if (++done_ticks >= 6) {   // ~1.8s at a 300ms timer
                    g_closing = true;
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
            } else {
                done_ticks = 0;
            }
            return 0;
        }
        case WM_CLOSE:
            g_closing = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

bool run_ui(App& app) {
    g_app = &app;
    g_played = false;
    g_closing = false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x17, 0x19, 0x1b));
    wc.lpszClassName = L"ProjectOreoLauncherV3";
    // Icon resource 1, the same one Explorer shows on the exe. Without these two the window
    // and its taskbar button get Windows' default application icon, however good the icon
    // baked into the executable is - the resource alone only covers the file in Explorer.
    // LoadImage with the real metrics picks the right size out of the icon instead of
    // stretching one, so the title bar and the taskbar each get a sharp version.
    wc.hIcon = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExW(&wc);

    RECT wanted{0, 0, 1180, 860};
    AdjustWindowRect(&wanted, WS_OVERLAPPEDWINDOW, FALSE);
    int window_width = wanted.right - wanted.left;
    int window_height = wanted.bottom - wanted.top;
    HMONITOR monitor = nullptr;
    if (HWND foreground = GetForegroundWindow())
        monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        POINT cursor{};
        GetCursorPos(&cursor);
        monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    GetMonitorInfoW(monitor, &monitor_info);
    RECT work = monitor_info.rcWork;
    int window_x = work.left + ((work.right - work.left) - window_width) / 2;
    int window_y = work.top + ((work.bottom - work.top) - window_height) / 2;
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"ProjectOreo Launcher", WS_OVERLAPPEDWINDOW,
                             window_x, window_y, window_width, window_height,
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) {
        Log::error("could not create the launcher window");
        return false;
    }
    BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE; ignored on older Windows builds.
    typedef HRESULT(WINAPI * SetAttr)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        if (auto set_attr = (SetAttr)GetProcAddress(dwm, "DwmSetWindowAttribute"))
            set_attr(g_hwnd, 20, &dark, sizeof(dark));
        FreeLibrary(dwm);
    }
    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    UpdateWindow(g_hwnd);

    // WebView2 keeps its cache/profile here rather than next to the game files.
    std::string data_dir = path_join(app.config().oreo_dir, "webview");
    make_dirs(data_dir);

    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, to_wide(data_dir).c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&ready, &failed](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    failed = true;
                    return S_OK;
                }
                env->CreateCoreWebView2Controller(
                    g_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&ready, &failed](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || !controller) {
                                failed = true;
                                return S_OK;
                            }
                            g_controller = controller;
                            g_controller->get_CoreWebView2(&g_webview);

                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(g_webview->get_Settings(&settings))) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(
                                    g_app->config().debug_hotkeys ? TRUE : FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
                            }

                            EventRegistrationToken token;
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                                        -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                            handle_message(to_utf8(raw));
                                            CoTaskMemFree(raw);
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);

                            RECT rc;
                            GetClientRect(g_hwnd, &rc);
                            g_controller->put_Bounds(rc);
                            g_webview->NavigateToString(to_wide(kUiHtml).c_str());
                            ready = true;
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(hr)) {
        Log::error("WebView2 is not available on this system (the Edge WebView2 runtime is missing)");
        MessageBoxW(g_hwnd,
                    L"ProjectOreo needs the Microsoft Edge WebView2 runtime, which is missing on this "
                    L"system.\n\nInstall it from:\nhttps://developer.microsoft.com/microsoft-edge/webview2/",
                    L"ProjectOreo", MB_OK | MB_ICONWARNING);
        DestroyWindow(g_hwnd);
        return false;
    }

    SetTimer(g_hwnd, 1, 300, nullptr);   // state refresh + play-finished check

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (ready.exchange(false)) push_state();   // first paint once the page is loaded
        if (failed.exchange(false)) {
            Log::error("WebView2 could not be created");
            break;
        }
        if (g_closing && !IsWindow(g_hwnd)) break;
    }

    KillTimer(g_hwnd, 1);
    g_webview.Reset();
    g_controller.Reset();
    g_app = nullptr;
    return g_played;
}

}  // namespace oreo
