  const OUTLINE = "0.5,0.5 385,0.5 404,14 625.5,14 625.5,105.5 0.5,105.5";

  // Mod pictures, by the key the state names them with. C++ sends each one once; the cards are
  // drawn again when one arrives or changes.
  const thumbs = {};
  function setThumb(key, uri) {
    thumbs[key] = uri;
    if (lastState) { cardsDrawn = ""; render(lastState); }
  }

  // A card shows at most 33 characters of a mod's name; a longer one is cut there and gets "...".
  const NAME_MAX = 33;
  function shortName(name) {
    const chars = Array.from(name || "");
    return chars.length > NAME_MAX ? chars.slice(0, NAME_MAX).join("") + "..." : chars.join("");
  }

  // One mod. Clicking the card opens its Details; its switch and its INSTALL / UPDATE button
  // keep their clicks to themselves. The first card is the one the design gives a tab with its
  // own SETTINGS (no action behind it yet, so hidden for now).
  function modCard(m, slot, busy) {
    const x = slot % 2 ? 682 : 0, y = Math.floor(slot / 2) * 152, deep = slot === 0;
    const pic = m.thumb && thumbs[m.thumb];
    const meta = m.version ? "v" + m.version + " · " + m.kind : m.kind + " · version unknown";
    let prep = "";
    if (m.preparing)
      prep = `<button class="install working" type="button" disabled>${DOTS}</button>`;
    else if (m.prep)
      prep = `<button class="install" type="button"${busy ? " disabled" : ""}
          onclick="event.stopPropagation(); send('prepare','${m.index}')">${esc(m.prep)}</button>`;
    return `<div class="slot" style="left:${x}px;top:${y}px">
      <div class="glass card${pic ? " pic" : ""}" onclick="openMod(${m.index})">
        ${pic ? `<div class="mod-pic" style="background-image:url(${pic})"></div>` : ""}
        <svg class="shape" width="626" height="106" viewBox="0 0 626 106" aria-hidden="true"><polygon
            points="${OUTLINE}"/></svg>
        <div class="card-status"></div>
        <div class="card-name">${esc(shortName(m.name))}</div>
        <div class="card-meta">${esc(meta)}</div>
        ${prep}
        <label class="tog" onclick="event.stopPropagation()"><input type="checkbox"${m.enabled ? " checked" : ""}${busy ? " disabled" : ""}
            onchange="send('togglemod','${m.index}')" aria-label="Toggle mod"><span class="track"><span
            class="t-on">ON</span><span class="t-off">OFF</span><span class="knob"></span></span></label>
      </div>
      ${deep ? `<button class="pwm notch" type="button" aria-label="Mod settings"><svg width="233" height="21"
          viewBox="0 0 233 21" aria-hidden="true"><polygon points="0.5,0.5 232.5,0.5 232.5,20.5 21,20.5"/></svg><span>SETTINGS</span></button>` : ""}
    </div>`;
  }

  // The cards are only rebuilt when something other than the switches changed. Flipping a
  // switch comes back from C++ as a new state at once, and rebuilding the card then would
  // replace the switch mid-slide and cut its animation off.
  let cardsDrawn = "";
  function drawCards(mods, busy) {
    const drawn = JSON.stringify([busy, mods.map(m => Object.assign({}, m, {enabled: null}))]);
    if (drawn !== cardsDrawn) {
      cardsDrawn = drawn;
      const list = el('cards'), scrolled = list.scrollTop;   // a repaint must not jump to the top
      list.innerHTML = mods.map((m, slot) => modCard(m, slot, busy)).join("");
      list.scrollTop = scrolled;
    }
    // Also puts a switch back when C++ refused to change it.
    el('cards').querySelectorAll('.tog input').forEach((input, slot) => {
      if (input.checked !== mods[slot].enabled) input.checked = mods[slot].enabled;
    });
  }

  function render(state) {
    // One file-touching operation at a time, so while any of them runs every control that
    // would start another goes flat. The C++ side refuses them regardless - this is so the
    // player can see why, instead of clicking something that quietly does nothing.
    const busy = !!state.busyWith;
    document.body.classList.toggle('maximized', !!state.maximized);
    document.body.classList.toggle('light', !!state.lightTheme);
    document.body.classList.toggle('hc', !!state.highContrast);
    el('invsw').checked = !state.lightTheme;

    el('svcgame').textContent = ("No Man's Sky: " + (state.gameVersion || "unknown")).toUpperCase();
    el('svcmods').textContent = "DETECTED MODS (" + state.mods.length + ")";
    el('svcversion').textContent = ("ProjectOreo " + state.version +
                                    (state.testMode ? " · test version" : "")).toUpperCase();
    el('testbtn').hidden = !!state.testMode;
    el('svcpython').textContent = (state.pythonVersion
        ? "Python: " + state.pythonVersion + " (bundled)" : "Python: missing").toUpperCase();

    drawCards(state.mods, busy);
    el('refresh').disabled = busy;
    el('refresh').classList.toggle('spin', state.refreshing);
    const upd = el('launcherupd'), updating = state.updatingLauncher;
    upd.hidden = !state.launcherUpdate && !updating;
    upd.disabled = busy;
    upd.title = state.launcherLatest ? "Update to " + state.launcherLatest : "";
    upd.innerHTML = updating ? DOTS : "Update launcher";
    lastState = state;
    drawMod();

    const play = el('play');
    play.textContent = state.playRunning ? "STARTING" : "PLAY";
    play.disabled = !!state.playRunning || busy;
    el('vanilla').disabled = play.disabled;
  }

