/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · realtime.js
   Charts históricos (TB Edge) + actualización en vivo vía WebSocket
   ═══════════════════════════════════════════════════════════════════ */

let ws = null;
let sensorChart = null;
let rssiChart = null;
const MAX_LIVE = 80;
const _lastVals = { sensor: {}, rssi: {} };
let _reconnectTimer = null;
let _currentDeviceId = null;
let _historyRefreshTimer = null;

function _cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function _periodMs(period) {
  if (period === '1h') return 3600000;
  if (period === '6h') return 21600000;
  return 86400000;
}

function _fmtTime(ts) {
  return new Date(ts).toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' });
}

function _fmtDateTime(ts) {
  return new Date(ts).toLocaleString('es-CO', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
}

function _sensorKeyFromId(deviceId) {
  const dev = App.state.devices.find(d => d.id === deviceId);
  if (!dev) return null;
  return App.getNodeSensorVar((dev.telemetry || {}), deviceId);
}

function createChart(canvasId, label, color, isRssi) {
  const ctx = document.getElementById(canvasId);
  if (!ctx) return null;
  const gridColor = _cssVar('--border');
  const tickColor = _cssVar('--text-dim');
  const textColor = _cssVar('--text-muted');
  return new Chart(ctx, {
    type: 'line',
    data: { labels: [], datasets: [{ label, data: [], borderColor: color, backgroundColor: color + '18', borderWidth: 2, pointRadius: isRssi ? 0 : 1.5, pointBackgroundColor: color, pointBorderColor: 'transparent', fill: true, tension: 0.3 }] },
    options: {
      responsive: true, maintainAspectRatio: false, animation: { duration: 200 },
      interaction: { intersect: false, mode: 'index' },
      scales: {
        x: { display: true, grid: { display: false }, ticks: { color: tickColor, font: { size: 8 }, maxTicksLimit: 8, maxRotation: 45 } },
        y: { display: true, grid: { color: gridColor }, ticks: { color: tickColor, font: { size: 9 }, maxTicksLimit: 4, callback: v => parseFloat(v).toFixed(1) }, beginAtZero: isRssi },
      },
      plugins: {
        legend: { display: false },
        tooltip: {
          enabled: true, backgroundColor: _cssVar('--surface'), titleColor: textColor,
          bodyColor: color, borderColor: gridColor, borderWidth: 1, cornerRadius: 6, padding: 8,
          callbacks: {
            title: items => items.length ? items[0].label : '',
            label: ctx => `${ctx.dataset.label}: ${parseFloat(ctx.raw).toFixed(1)}`,
          },
        },
      },
    },
  });
}

async function fetchHistory(deviceId, period, sensorKey) {
  const now = Date.now();
  const startTs = now - _periodMs(period);
  const interval = period === '24h' ? 600000 : period === '6h' ? 300000 : 60000;
  const keys = sensorKey + ',rssi';
  const url = `/api/telemetry/${deviceId}/history?keys=${keys}&startTs=${startTs}&endTs=${now}&agg=AVG&interval=${interval}`;
  try {
    const res = await App.api(url);
    if (!res) return { sensor: [], rssi: [] };
    const data = await res.json();
    const sensorPts = (data[sensorKey] || []).map(p => ({ ts: p.ts, v: parseFloat(p.value) })).filter(p => !isNaN(p.v));
    const rssiPts = (data.rssi || []).map(p => ({ ts: p.ts, v: parseFloat(p.value) })).filter(p => !isNaN(p.v));
    return { sensor: sensorPts, rssi: rssiPts };
  } catch(e) { console.error('[hist]', e); return { sensor: [], rssi: [] }; }
}

function loadHistoryIntoChart(chart, points) {
  if (!chart) return;
  chart.data.labels = points.map(p => _fmtDateTime(p.ts));
  chart.data.datasets[0].data = points.map(p => p.v);
  chart.update('none');
}

function appendLive(chart, key, ts, value) {
  if (!chart || value === undefined || value === null) return;
  const v = parseFloat(value);
  if (isNaN(v)) return;
  if (_lastVals[key] && v === _lastVals[key].val && (Date.now() - _lastVals[key].ts) < 10000) return;
  _lastVals[key] = { val: v, ts: Date.now() };

  const label = _fmtTime(ts);
  chart.data.labels.push(label);
  chart.data.datasets[0].data.push(v);

  const total = chart.data.labels.length;
  if (total > MAX_LIVE + 50) {
    const remove = total - MAX_LIVE;
    chart.data.labels.splice(0, remove);
    chart.data.datasets[0].data.splice(0, remove);
  }
  chart.update('none');
}

function _ensureSensorChart(sensorKey) {
  if (sensorChart) return;
  if (!document.getElementById('chart-sensor')) return;
  const sv = App.SENSOR_VARS[sensorKey];
  if (!sv) return;
  const header = document.querySelector('#chart-sensor').closest('.chart-container').querySelector('h4');
  if (header) header.innerHTML = `<span style="color:${sv.color}">${sv.icon}</span> ${sv.label}`;
  sensorChart = createChart('chart-sensor', `${sv.icon} ${sv.label}`, sv.color, false);
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
      const now = Date.now();

      const sk = _dataSensorKey(data);
      if (sk) {
        App.setLiveVar(deviceId, sk);
        _ensureSensorChart(sk);
        appendLive(sensorChart, 'sensor', now, data[sk]);
      }
      appendLive(rssiChart, 'rssi', now, data.rssi);

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

async function startRealtimeCharts(deviceId, period) {
  if (_reconnectTimer) { clearTimeout(_reconnectTimer); _reconnectTimer = null; }
  if (_historyRefreshTimer) { clearInterval(_historyRefreshTimer); _historyRefreshTimer = null; }
  _currentDeviceId = deviceId;

  try {
    if (sensorChart) sensorChart.destroy();
    if (rssiChart) rssiChart.destroy();
    sensorChart = null;
    rssiChart = null;
  } catch(e) { console.error(e); }

  const sensorKey = _sensorKeyFromId(deviceId);
  const sv = sensorKey ? App.SENSOR_VARS[sensorKey] : null;

  if (sv && document.getElementById('chart-sensor'))
    sensorChart = createChart('chart-sensor', `${sv.icon} ${sv.label}`, sv.color, false);
  if (document.getElementById('chart-rssi'))
    rssiChart = createChart('chart-rssi', 'RSSI', '#a78bfa', true);

  const hist = await fetchHistory(deviceId, period || '1h', sensorKey || 'temperature');
  if (sv) loadHistoryIntoChart(sensorChart, hist.sensor);
  loadHistoryIntoChart(rssiChart, hist.rssi);

  _historyRefreshTimer = setInterval(async () => {
    const sk = _sensorKeyFromId(deviceId);
    const h = await fetchHistory(deviceId, period || '1h', sk || 'temperature');
    if (sk) loadHistoryIntoChart(sensorChart, h.sensor);
    loadHistoryIntoChart(rssiChart, h.rssi);
  }, 60000);

  connectWS(deviceId);
}
