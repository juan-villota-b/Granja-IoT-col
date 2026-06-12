/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · realtime.js
   WebSocket + Chart.js en tiempo real — solo sensor + RSSI
   ═══════════════════════════════════════════════════════════════════ */

let ws = null;
let sensorChart = null;
let rssiChart = null;
const MAX_POINTS = 20;
const _lastVals = { sensor: {}, rssi: {} };
let _reconnectTimer = null;
let _currentDeviceId = null;

function _cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function createHistoryChart(canvasId, label, color) {
  const ctx = document.getElementById(canvasId);
  if (!ctx) return null;
  const gridColor = _cssVar('--border');
  const tickColor = _cssVar('--text-dim');
  const textColor = _cssVar('--text-muted');
  return new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: [{
        label,
        data: [],
        borderColor: color,
        backgroundColor: color + '18',
        borderWidth: 2,
        pointRadius: 3,
        pointBackgroundColor: color,
        pointBorderColor: 'transparent',
        fill: true,
        tension: 0.3,
      }],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 200 },
      interaction: { intersect: false, mode: 'index' },
      scales: {
        x: {
          display: true,
          grid: { display: false },
          ticks: {
            color: tickColor,
            font: { size: 9 },
            maxTicksLimit: 6,
            maxRotation: 45,
          },
        },
        y: {
          display: true,
          grid: { color: gridColor },
          ticks: {
            color: tickColor,
            font: { size: 9 },
            maxTicksLimit: 4,
            callback: v => parseFloat(v).toFixed(1),
          },
        },
      },
      plugins: {
        legend: { display: false },
        tooltip: {
          enabled: true,
          backgroundColor: _cssVar('--surface'),
          titleColor: textColor,
          bodyColor: color,
          borderColor: gridColor,
          borderWidth: 1,
          cornerRadius: 6,
          padding: 8,
          callbacks: {
            label: ctx => `${ctx.dataset.label}: ${parseFloat(ctx.raw).toFixed(1)}`,
          },
        },
      },
    },
  });
}

function _ensureSensorChart(sensorKey) {
  if (sensorChart) return;
  if (!document.getElementById('chart-sensor')) return;
  const sv = App.SENSOR_VARS[sensorKey];
  if (!sv) return;
  const header = document.querySelector('#chart-sensor').closest('.chart-container').querySelector('h4');
  if (header) header.innerHTML = `<span style="color:${sv.color}">${sv.icon}</span> ${sv.label}`;
  sensorChart = createHistoryChart('chart-sensor', `${sv.icon} ${sv.label}`, sv.color);
}

function _dataSensorKey(data) {
  if (data.temperature !== undefined && data.temperature !== null) return 'temperature';
  if (data.humidity !== undefined && data.humidity !== null) return 'humidity';
  if (data.light !== undefined && data.light !== null) return 'light';
  return null;
}

function connectWS(deviceId) {
  if (ws) { try { ws.close(); } catch(e) { console.error(e); } }
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${protocol}//${window.location.host}/ws/${deviceId}`);
  _currentDeviceId = deviceId;

  ws.onmessage = event => {
    try {
      const data = JSON.parse(event.data);
      const now = new Date().toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit', second: '2-digit' });

      const sk = _dataSensorKey(data);
      if (sk) {
        _ensureSensorChart(sk);
        addPoint(sensorChart, 'sensor', now, data[sk]);
      }
      addPoint(rssiChart, 'rssi', now, data.rssi);

      if (typeof updateMarkerTelemetry === 'function') updateMarkerTelemetry(deviceId, data);

      const dev = App.state.devices.find(d => d.id === deviceId);
      if (dev) {
        if (!dev.telemetry) dev.telemetry = {};
        if (data.temperature !== undefined) dev.telemetry.temperature = data.temperature;
        if (data.humidity !== undefined) dev.telemetry.humidity = data.humidity;
        if (data.light !== undefined) dev.telemetry.light = data.light;
        if (data.rssi !== undefined) dev.telemetry.rssi = data.rssi;
        if (data._ts !== undefined) dev.telemetry._ts = data._ts;
        if (data._lastActivityTime !== undefined) dev.attributes.lastActivityTime = data._lastActivityTime;
        if (App.state.activeNodeId === deviceId) App.renderNodeInfo(dev);
      }
    } catch(e) { console.error('[ws] onmessage:', e); }
  };

  ws.onclose = () => { ws = null; scheduleReconnect(); };
  ws.onerror = () => { console.error('[ws] error on', deviceId); };
}

function scheduleReconnect() {
  if (_reconnectTimer) return;
  if (!_currentDeviceId) return;
  _reconnectTimer = setTimeout(() => {
    _reconnectTimer = null;
    if (!ws && _currentDeviceId) connectWS(_currentDeviceId);
  }, 3000);
}

function startRealtimeCharts(deviceId) {
  if (_reconnectTimer) { clearTimeout(_reconnectTimer); _reconnectTimer = null; }
  _currentDeviceId = deviceId;

  try {
    if (sensorChart) sensorChart.destroy();
    if (rssiChart) rssiChart.destroy();
    sensorChart = null;
    rssiChart = null;
  } catch(e) { console.error(e); }

  if (document.getElementById('chart-rssi'))
    rssiChart = createHistoryChart('chart-rssi', 'RSSI', '#a78bfa');

  connectWS(deviceId);
}

function addPoint(chart, key, label, value) {
  if (!chart || value === undefined || value === null) return;
  const v = parseFloat(value);
  if (isNaN(v)) return;

  if (_lastVals[key] && v === _lastVals[key].val) return;
  _lastVals[key] = { val: v, ts: Date.now() };

  chart.data.labels.push(label);
  chart.data.datasets[0].data.push(v);
  while (chart.data.labels.length > MAX_POINTS) {
    chart.data.labels.shift();
    chart.data.datasets[0].data.shift();
  }
  chart.update('none');
}
