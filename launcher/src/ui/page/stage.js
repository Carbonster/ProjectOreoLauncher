  function send(action, id) {
    window.chrome.webview.postMessage(JSON.stringify({action: action, id: id || ""}));
  }
  // The Settings form carries more than one value, so it posts a whole object. Every field is
  // a string: the C++ side reads this JSON with a deliberately tiny parser.
  function sendObj(message) { window.chrome.webview.postMessage(JSON.stringify(message)); }
  const esc = s => (s == null ? "" : String(s).replace(/[&<>"]/g,
      c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])));
  const el = id => document.getElementById(id);
  const DOTS = '<span class="dots"><i></i><i></i><i></i></span>';

  // ---- the stage: scaled to the window as a whole, never rearranged -----------------------
  const STAGE_W = 1568, STAGE_H = 906;
  function fit() {
    const k = Math.min(innerWidth / STAGE_W, innerHeight / STAGE_H);
    el('stage').style.transform = "scale(" + k + ")";
    document.body.style.setProperty('--k', k);   // the mod sheet scales with the stage
  }
  addEventListener('resize', fit);
  fit();

  document.querySelectorAll('.edge').forEach(edge => edge.addEventListener('mousedown', e => {
    if (e.button !== 0) return;
    e.preventDefault();
    send('resize', edge.dataset.edge);
  }));

  // A press on the background moves the window: the window is asked to run its own move, the
  // one its title bar would start. The second press of a double click maximises or restores it
  // instead - the move keeps the first release for itself, so `dblclick` cannot be relied on.
  // The dimmed ground around an open window counts as background too: a press there lands on
  // the <dialog> itself, outside its box.
  const onControl = e => e.target.closest('button, label, input, .cards, dialog');
  const onBackdrop = e => {
    if (e.target.tagName !== 'DIALOG') return false;
    const r = e.target.getBoundingClientRect();
    return e.clientX < r.left || e.clientX >= r.right || e.clientY < r.top || e.clientY >= r.bottom;
  };
  document.addEventListener('mousedown', e => {
    if (e.button !== 0) return;
    if (!onBackdrop(e) && !(e.target.closest('#frame') && !onControl(e))) return;
    e.preventDefault();   // no text selection, no focus change - as on a real title bar
    send(e.detail === 2 ? 'maximize' : 'drag');
  });

  // The switch says "inverted" when it is on, and the inverted look is the dark one.
  function setTheme(dark) {
    document.body.classList.toggle('light', !dark);
    send('theme', dark ? 'dark' : 'light');
  }

