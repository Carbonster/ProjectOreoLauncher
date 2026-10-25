  // ---- a mod, opened ---------------------------------------------------------------------
  // Drawn here from the state rather than handed over as finished HTML, so it keeps up while it
  // is open: an UPDATE pressed on the sheet runs, and the sheet shows the dots and then the new
  // version. `openedMod` is the mod's index, as on its card; null while the sheet is shut.
  let lastState = null, openedMod = null;

  function openMod(index) {
    openedMod = index;
    drawMod();
    if (openedMod === null) return;
    el('moddlg').showModal();
    el('moddlg').focus();
  }
  el('moddlg').addEventListener('close', () => { openedMod = null; });

  function reqRow(c, busy) {
    let act = "";
    if (c.busy)
      act = `<button class="install working" type="button" disabled>${DOTS}</button>`;
    else if (c.action)
      act = `<button class="install" type="button"${busy ? " disabled" : ""}
          onclick="send('action','${c.id}')">${esc(c.action)}</button>`;
    else if (c.upToDate)
      act = `<span class="rok">UP TO DATE</span>`;
    const note = c.note ? `<div class="rnote">${esc(c.note)}${c.details
        ? `<a onclick="send('details','${c.id}')">Details</a>` : ""}</div>` : "";
    const about = c.repo
      ? `<div class="rabout"><button class="rsub" type="button" onclick="send('openurl','${esc(c.repo)}')"
            >${esc(c.subtitle)}</button><div class="rtip"><p>${esc(c.about)}</p>
            <div>${c.owner ? "By " + esc(c.owner) + " · " : ""}Opens on GitHub</div></div></div>`
      : `<div class="rabout"><span class="rsub">${esc(c.subtitle)}</span></div>`;
    return `<div class="req">
      <div><div class="rname">${esc(c.name)}</div>${about}</div>
      <div><div class="rcap">Installed</div>
        <div class="rval${c.installed ? "" : " muted"}">${esc(c.installed || "none")}</div></div>
      <div><div class="rcap">Latest</div>
        <div class="rval${c.latest ? "" : " muted"}">${esc(c.latest || "unknown")}</div></div>
      ${act || "<div></div>"}${note}</div>`;
  }

  function drawMod() {
    if (openedMod === null || !lastState) return;
    const m = lastState.mods.find(m => m.index === openedMod);
    if (!m) { el('moddlg').close(); return; }   // gone from the folder while it was open
    const busy = !!lastState.busyWith;
    const pic = m.thumb && thumbs[m.thumb];
    el('modpic').hidden = !pic;
    // Set only when it changes: a picture is a megabyte or two of text for the style to parse.
    if (el('modpic').dataset.key !== (pic ? m.thumb : "")) {
      el('modpic').dataset.key = pic ? m.thumb : "";
      el('modpic').style.backgroundImage = pic ? `url(${pic})` : "";
    }
    el('modname').textContent = m.name;
    el('modmeta').textContent = m.version ? "v" + m.version + " · " + m.kind : m.kind + " · version unknown";
    el('moddesc').textContent = m.description || "";
    el('moddesc').hidden = !m.description;
    el('moddebug').hidden = !lastState.debugHotkeys;
    el('moddebug').innerHTML = lastState.debugHotkeys
      ? `Entry point: ${esc(m.entryPoint || "unknown")}<br>Metadata file: ${esc(m.manifest || "none")}` : "";
    const reqs = m.reqs.map(id => lastState.components.find(c => c.id === id)).filter(Boolean);
    el('modreqlabel').hidden = !reqs.length;
    el('modreqs').innerHTML = reqs.map(c => reqRow(c, busy)).join("");
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
    el('settingsdlg').focus();
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
