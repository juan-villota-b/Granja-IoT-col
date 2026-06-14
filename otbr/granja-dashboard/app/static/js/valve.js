/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · valve.js
   Control de válvulas vía RPC — sin fetches redundantes
   ═══════════════════════════════════════════════════════════════════ */

let valveCheckInterval = null;
let _valveDeviceId = null;

function initValvula() {
  if (valveCheckInterval) clearInterval(valveCheckInterval);

  document.getElementById('btn-valve-on')?.addEventListener('click', () => sendValveCommand(1));
  document.getElementById('btn-valve-off')?.addEventListener('click', () => sendValveCommand(0));

  checkValveNodes();
  valveCheckInterval = setInterval(checkValveNodes, 15000);
}

async function checkValveNodes() {
  if (App.state.devices.length === 0) {
    const res = await App.api('/api/devices');
    if (!res) return;
    const data = await res.json();
    App.state.devices = data.devices || [];
  }

  const valveDevices = App.state.devices.filter(
    d => (d.type || '').toLowerCase().includes('valve') || (d.name || '').toLowerCase().includes('valve')
  );

  const statusText = document.getElementById('valve-status-text');
  const statusIndicator = document.getElementById('valve-status-indicator');
  const deviceName = document.getElementById('valve-device-name');
  const btnOn = document.getElementById('btn-valve-on');
  const btnOff = document.getElementById('btn-valve-off');

  if (valveDevices.length > 0) {
    const valve = valveDevices[0];
    _valveDeviceId = valve.id;
    const state = valve.telemetry?.valve;
    const isOpen = state === '1' || state === 1 || state === true;

    if (statusIndicator) {
      const dot = statusIndicator.querySelector('.valvula-state-dot');
      if (dot) dot.style.background = isOpen ? 'var(--success)' : 'var(--danger)';
    }
    if (statusText) statusText.textContent = isOpen ? 'Válvula ABIERTA' : 'Válvula CERRADA';
    if (deviceName) deviceName.textContent = valve.name || '';

    btnOn.disabled = false;
    btnOff.disabled = false;
  } else {
    if (statusIndicator) {
      const dot = statusIndicator.querySelector('.valvula-state-dot');
      if (dot) dot.style.background = 'var(--text-dim)';
    }
    if (statusText) statusText.textContent = 'Desconectada';
    if (deviceName) deviceName.textContent = 'Esperando nodo válvula...';
    btnOn.disabled = true;
    btnOff.disabled = true;
  }
}

async function sendValveCommand(state) {
  if (!_valveDeviceId) {
    App.toast('No se encontró nodo válvula', 'error');
    return;
  }

  const feedback = document.getElementById('valve-feedback');
  const label = state === 1 ? 'ABRIR' : 'CERRAR';
  if (feedback) feedback.textContent = `Enviando comando ${label}...`;

  const res = await App.api('/api/rpc/valve', {
    method: 'POST',
    body: JSON.stringify({ device_id: _valveDeviceId, state }),
  });

  if (res && res.ok) {
    const result = await res.json();
    if (result.ok) {
      App.toast(`Comando ${label} enviado exitosamente`, 'success');
      if (feedback) feedback.textContent = '';
    } else {
      App.toast(`Error al enviar comando ${label}`, 'error');
      if (feedback) feedback.textContent = `Error: el servidor rechazó el comando`;
    }
  } else {
    App.toast('Error de conexión', 'error');
    if (feedback) feedback.textContent = 'Error de conexión al enviar comando';
  }

  setTimeout(() => checkValveNodes(), 2000);
}