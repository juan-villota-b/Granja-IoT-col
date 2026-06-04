/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · realtime.js
   WebSocket + Chart.js sparklines en tiempo real
   ═══════════════════════════════════════════════════════════════════ */

let ws = null;
let tempChart = null;
let humChart = null;
let battChart = null;
const MAX_POINTS = 60;
let _lastVals = {};
let _reconnectTimer = null;
let _currentDeviceId = null;

['temp', 'hum', 'batt'].forEach(k => _lastVals[k] = { val: null, ts: 0 });

function _cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function createSimpleChart(canvasId, label, color) {
  const ctx = document.getElementById(canvasId);
  if (!ctx) return null;
  const gridColor = _cssVar('--border');
  const tickColor = _cssVar('--text-dim');
  return new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: [{
        label,
        data: [],
        borderColor: color,
        backgroundColor: color + '18',
        borderWidth: 1.5,
        pointRadius: 0,
        fill: true,
        tension: 0.4,
      }],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 150 },
      interaction: { intersect: false, mode: 'index' },
      scales: {
        x: {
          display: false,
          grid: { display: false },
        },
        y: {
          grid: { color: gridColor },
          ticks: { color: tickColor, font: { size: 9 }, maxTicksLimit: 3, callback: v => v.toFixed(1) },
        },
      },
      plugins: { legend: { display: false }, tooltip: { enabled: false } },
    },
  });
}

function connectWS(deviceId) {
  if (ws) {
    try { ws.close(); } catch(e) { console.error('[ws] close:', e); }
  }
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${protocol}//${window.location.host}/ws/${deviceId}`);
  _currentDeviceId = deviceId;

  ws.onmessage = event => {
    try {
      const data = JSON.parse(event.data);
      const now = new Date().toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
      addPoint(tempChart, now, data.temperature, 'temp');
      addPoint(humChart, now, data.humidity, 'hum');
      addPoint(battChart, now, data.battery, 'batt');

      if (typeof updateMarkerTelemetry === 'function') {
        updateMarkerTelemetry(deviceId, data);
      }

      const dev = App.state.devices.find(d => d.id === deviceId);
      if (dev) {
        if (!dev.telemetry) dev.telemetry = {};
        if (data.temperature !== undefined) dev.telemetry.temperature = data.temperature;
        if (data.humidity !== undefined) dev.telemetry.humidity = data.humidity;
        if (data.battery !== undefined) dev.telemetry.battery = data.battery;
        if (data.rssi !== undefined) dev.telemetry.rssi = data.rssi;
        if (data.uptime !== undefined) dev.telemetry.uptime = data.uptime;
        if (data._ts !== undefined) dev.telemetry._ts = data._ts;
        if (App.state.activeNodeId === deviceId) {
          App.renderNodeInfo(dev);
        }
      }
    } catch(e) { console.error('[ws] onmessage:', e); }
  };

  ws.onclose = () => {
    ws = null;
    scheduleReconnect();
  };

  ws.onerror = () => {
    console.error('[ws] error on', deviceId);
  };
}

function scheduleReconnect() {
  if (_reconnectTimer) return;
  if (!_currentDeviceId) return;
  _reconnectTimer = setTimeout(() => {
    _reconnectTimer = null;
    if (!ws && _currentDeviceId) {
      connectWS(_currentDeviceId);
    }
  }, 3000);
}

function startRealtimeCharts(deviceId) {
  if (_reconnectTimer) { clearTimeout(_reconnectTimer); _reconnectTimer = null; }

  if (!document.getElementById('chart-temp')) return;
  _currentDeviceId = deviceId;

  try {
    if (tempChart) tempChart.destroy();
    if (humChart) humChart.destroy();
    if (battChart) battChart.destroy();
  } catch(e) { console.error('[charts] destroy:', e); }

  tempChart = createSimpleChart('chart-temp', 'Temperatura', '#f59e0b');
  humChart = createSimpleChart('chart-hum', 'Humedad', '#06b6d4');
  battChart = createSimpleChart('chart-batt', 'Batería', '#22c55e');

  connectWS(deviceId);
}

function addPoint(chart, label, value, key) {
  if (!chart || value === undefined || value === null) return;
  const parsed = parseFloat(value);
  if (isNaN(parsed)) return;
  const now = Date.now();

  const last = _lastVals[key];
  if (key && last) {
    if (parsed === last.val && (now - last.ts) < 30000) return;
  }
  if (key) _lastVals[key] = { val: parsed, ts: now };

  chart.data.labels.push(label);
  chart.data.datasets[0].data.push(parsed);
  if (chart.data.labels.length > MAX_POINTS) {
    chart.data.labels.shift();
    chart.data.datasets[0].data.shift();
  }
  chart.update('none');
}