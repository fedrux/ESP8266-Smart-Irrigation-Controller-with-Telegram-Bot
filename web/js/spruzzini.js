(function () {
  'use strict';
  var state = null, csrf = '', pending = false, loaded = false, timer = null;
  function el(id) { return document.getElementById(id); }
  function feedback(message, error) { el('feedback').textContent = message; el('feedback').className = error ? 'feedback error' : 'feedback'; }
  function controls(enabled) {
    ['start', 'save', 'reset'].forEach(function (id) { el(id).disabled = !enabled || pending; });
    if (state && (state.storage_error || state.application_ready === false)) el('start').disabled = true;
    el('stop').disabled = !state || pending;
  }
  function confirmation(message, action) {
    var dialog = el('confirm'); el('confirm-text').textContent = message;
    dialog.returnValue = ''; dialog.onclose = function () { if (dialog.returnValue === 'confirm') action(); };
    dialog.showModal();
  }
  function render(data) {
    state = data; csrf = data.csrf_token;
    var active = data.cycle_active || data.single_active;
    el('status').textContent = data.status;
    el('signal').className = 'signal ' + (data.storage_error ? 'bad' : active ? 'warn' : 'good');
    el('clock').textContent = data.clock_valid ? data.clock + ' · orario v8 (UTC+2)' : 'Orario NTP non ancora sincronizzato';
    el('updated').textContent = 'Ultimo contatto: ' + new Date().toLocaleTimeString('it-IT');
    el('sector').textContent = active ? String(data.sector) + ' / 5' : 'A riposo';
    el('remaining').textContent = active ? Math.floor(data.remaining_seconds / 60) + 'm ' + data.remaining_seconds % 60 + 's' : '—';
    el('schedule').textContent = data.configured && data.automatic ? String(data.start_hour).padStart(2, '0') + ':00' : 'Disattivo';
    el('smart').textContent = data.smart ? 'Attivo · 6h' : 'Disattivo';
    Array.prototype.forEach.call(el('sectors').children, function (item, index) {
      item.className = data.outputs[index] ? 'active' : '';
      item.querySelector('strong').textContent = data.outputs[index] ? 'Acceso' : 'Spento';
    });
    el('configured').textContent = data.configured ? 'Programma salvato sul dispositivo.' : 'Nessun programma configurato.';
    el('last-cycle').textContent = 'Ultimo ciclo (AAAAMMGGOO): ' + (data.last_cycle || 'nessuno');
    el('storage').textContent = data.storage_error || 'Impostazioni separate dalla configurazione di rete.';
    if (!loaded) {
      el('automatic').checked = data.automatic; el('weather').checked = data.smart;
      el('hour').value = data.start_hour; el('duration').value = data.minutes || 10; el('rain').value = data.rain_hours_saved;
      loaded = true;
    }
    controls(!active && data.storage_ready);
  }
  async function request(url, options) {
    var response = await fetch(url, Object.assign({credentials: 'same-origin', cache: 'no-store', signal: AbortSignal.timeout(15000)}, options));
    if (response.status === 401) throw new Error('Accesso richiesto: ricarica la pagina e inserisci le credenziali.');
    var body = await response.json();
    if (!response.ok) throw new Error(body.message || 'Operazione non riuscita');
    return body;
  }
  async function refresh() {
    if (pending) return;
    clearTimeout(timer);
    try { render(await request('/api/spruzzini/status')); if (el('feedback').className === 'feedback error') feedback(''); }
    catch (error) { state = null; csrf = ''; controls(false); el('status').textContent = 'Dispositivo non raggiungibile'; el('signal').className = 'signal bad'; feedback(error.message, true); }
    timer = setTimeout(refresh, 3000);
  }
  async function command(values) {
    if (pending || !csrf) return;
    pending = true; clearTimeout(timer); controls(false); feedback('Invio comando…');
    try {
      var result = await request('/api/spruzzini/command', {method: 'POST', headers: {'X-CSRF-Token': csrf, 'Content-Type': 'application/x-www-form-urlencoded'}, body: new URLSearchParams(values).toString()});
      feedback(result.message); if (values.action === 'reset' || values.action === 'settings') loaded = false;
    } catch (error) { feedback(error.message + ' Verifica lo stato prima di riprovare.', true); }
    finally { pending = false; refresh(); }
  }
  el('manual').addEventListener('submit', function (event) {
    event.preventDefault(); if (!state) return;
    var selection = el('selection').value, minutes = el('minutes').value;
    confirmation('Avviare ' + (selection === 'all' ? 'i cinque settori in sequenza' : 'il settore ' + selection) + ' per ' + minutes + ' minuti per settore?', function () {
      command({action: selection === 'all' ? 'cycle' : 'sector', minutes: minutes, sector: selection});
    });
  });
  el('stop').addEventListener('click', function () { command({action: 'stop'}); });
  el('settings').addEventListener('submit', function (event) {
    event.preventDefault(); command({action: 'settings', automatic: el('automatic').checked ? '1' : '0', smart: el('weather').checked ? '1' : '0', hour: el('hour').value, minutes: el('duration').value, rain_hours: el('rain').value});
  });
  el('reset').addEventListener('click', function () { confirmation('Ripristinare i valori predefiniti e rimuovere il programma salvato?', function () { command({action: 'reset'}); }); });
  refresh();
}());
