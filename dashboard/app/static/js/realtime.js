/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · realtime.js
   Chart con eje X lineal (scatter) + ventana fija de 1h que se desplaza
   ═══════════════════════════════════════════════════════════════════ */

let ws = null;
let sensorChart = null;
let rssiChart = null;
let _chartKey = null;
let _currentPeriod = '1h';
let _reconnectTimer = null;
let _currentDeviceId = null;
let _slideInterval = null;

const _lastVals = { sensor: {}, rssi: {} };

function _periodMs(p) {
  if (p === '1h') return 3600000;
  if (p === '6h') return 21600000;
  return 86400000;
}

function _windowMs() { return _periodMs(_currentPeriod); }

function _updateSensorHeader(key) {
  const sv = App.SENSOR_VARS[key];
  if (!sv) return;
  const h = document.querySelector('#chart-sensor')?.closest('.chart-container')?.querySelector('h4');
  if (h) h.innerHTML = `<span style="color:${sv.color}">${sv.icon}</span> ${sv.label}`;
}

function _tickFmt(v) {
  const d = new Date(v);
  if (_currentPeriod === '24h')
    return d.toLocaleDateString('es-CO', { day: 'numeric', month: 'short', hour: '2-digit' });
  return d.toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' });
}

function _ttFmt(v) {
  return new Date(v).toLocaleString('es-CO', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function _trim(chart) {
  if (!chart) return;
  const pts = chart.data.datasets[0].data;
  if (!pts.length) return;
  const cutoff = Date.now() - _windowMs();
  let n = 0;
  for (let i = 0; i < pts.length; i++) {
    if (pts[i].x < cutoff) n++; else break;
  }
  if (n > 0) pts.splice(0, n);
}

function _slide() {
  const now = Date.now();
  const w = _windowMs();
  [sensorChart, rssiChart].forEach(chart => {
    if (!chart) return;
    const sc = chart.options.scales.x;
    sc.min = now - w;
    sc.max = now;
    chart.update('none');
  });
}

function createChart(canvasId, label, color, isRssi) {
  const ctx = document.getElementById(canvasId);
  if (!ctx) return null;
  const grid = App.cssVar('--border');
  const tick = App.cssVar('--text-dim');
  const txt  = App.cssVar('--text-muted');
  const now  = Date.now();
  const w    = _windowMs();
  return new Chart(ctx, {
    type: 'line',
    data: {
      datasets: [{
        label, data: [], parsing: { xAxisKey: 'x', yAxisKey: 'y' },
        borderColor: color, backgroundColor: color + '18',
        borderWidth: 2, pointRadius: isRssi ? 0 : 1.5,
        pointBackgroundColor: color, pointBorderColor: 'transparent',
        fill: true, tension: 0.3,
      }]
    },
    options: {
      responsive: true, maintainAspectRatio: false, animation: false,
      layout: { padding: { bottom: 8, left: 4, right: 8 } },
      interaction: { intersect: false, mode: 'index' },
      scales: {
        x: {
          type: 'linear',
          min: now - w, max: now,
          grid: { display: false },
          ticks: { color: tick, font: { size: 10 }, maxTicksLimit: 6, callback: v => _tickFmt(v) },
        },
        y: {
          display: true, beginAtZero: isRssi,
          grid: { color: grid },
          ticks: { color: tick, font: { size: 10 }, maxTicksLimit: 4, callback: v => parseFloat(v).toFixed(1) },
        },
      },
      plugins: {
        legend: { display: false },
        tooltip: {
          enabled: true, backgroundColor: App.cssVar('--surface'),
          titleColor: txt, bodyColor: color, borderColor: grid,
          borderWidth: 1, cornerRadius: 6, padding: 8,
          callbacks: {
            title: items => items.length ? _ttFmt(items[0].parsed.x) : '',
            label: ctx => `${ctx.dataset.label}: ${parseFloat(ctx.parsed.y).toFixed(1)}`,
          },
        },
      },
    },
  });
}

async function fetchHistory(deviceId, period, sensorKey) {
  const now = Date.now();
  const startTs = now - _periodMs(period);
  const interval = period === '1h' ? 0 : period === '6h' ? 300000 : 600000;
  const keys = sensorKey + ',rssi';
  let url = `/api/telemetry/${deviceId}/history?keys=${keys}&startTs=${startTs}&endTs=${now}&limit=1000`;
  if (interval > 0) url += `&agg=AVG&interval=${interval}`;
  try {
    const r = await App.api(url);
    if (!r) return { sensor: [], rssi: [], key: null };
    const data = await r.json();
    const dk = App.getNodeSensorVar(data, deviceId);
    if (dk) sensorKey = dk;
    const sp = (data[sensorKey] || []).map(p => ({ x: p.ts, y: parseFloat(p.value) })).filter(p => !isNaN(p.y));
    const rp = (data.rssi || []).map(p => ({ x: p.ts, y: parseFloat(p.value) })).filter(p => !isNaN(p.y));
    return { sensor: sp, rssi: rp, key: sensorKey };
  } catch(e) { console.error('[hist]', e); return { sensor: [], rssi: [], key: null }; }
}

function loadPoints(chart, pts) {
  if (!chart) return;
  chart.data.datasets[0].data.length = 0;
  for (const p of pts) chart.data.datasets[0].data.push(p);
  _trim(chart);
  chart.update('none');
}

function pushPoint(chart, key, ts, value) {
  if (!chart || value == null) return;
  const v = parseFloat(value);
  if (isNaN(v)) return;
  const prev = _lastVals[key];
  if (prev && v === prev.val && Date.now() - prev.ts < 10000) return;
  _lastVals[key] = { val: v, ts: Date.now() };

  chart.data.datasets[0].data.push({ x: ts, y: v });
  _trim(chart);
  chart.update('none');
}

function setChartSensor(key) {
  const sv = App.SENSOR_VARS[key];
  if (!sv || !sensorChart) return;
  sensorChart.data.datasets[0].label = `${sv.icon} ${sv.label}`;
  sensorChart.data.datasets[0].borderColor = sv.color;
  sensorChart.data.datasets[0].backgroundColor = sv.color + '18';
  sensorChart.data.datasets[0].pointBackgroundColor = sv.color;
}

function connectWS(deviceId) {
  if (ws) { try { ws.close(); } catch(e) {} }
  _currentDeviceId = deviceId;
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${proto}//${location.host}/ws/${deviceId}`);

  ws.onmessage = e => {
    if (deviceId !== _currentDeviceId) return;
    try {
      const d = JSON.parse(e.data);
      const sk = App.getNodeSensorVar(d, deviceId);
      if (sk) {
        App.setLiveVar(deviceId, sk);
        if (sk !== _chartKey) { _chartKey = sk; setChartSensor(sk); _updateSensorHeader(sk); }
        const ts = (d._ts != null) ? parseInt(d._ts) : Date.now();
        pushPoint(sensorChart, 'sensor', isNaN(ts) ? Date.now() : ts, d[sk]);
      }
      const rts = (d._ts != null) ? parseInt(d._ts) : Date.now();
      pushPoint(rssiChart, 'rssi', isNaN(rts) ? Date.now() : rts, d.rssi);

      if (typeof updateMarkerTelemetry === 'function') updateMarkerTelemetry(deviceId, d);
      const dev = App.state.devices.find(x => x.id === deviceId);
      if (dev) {
        if (!dev.telemetry) dev.telemetry = {};
        if (d.temperature != null) dev.telemetry.temperature = d.temperature;
        if (d.humidity    != null) dev.telemetry.humidity    = d.humidity;
        if (d.light       != null) dev.telemetry.light       = d.light;
        if (d.rssi        != null) dev.telemetry.rssi        = d.rssi;
        if (d._ts         != null) dev.telemetry._ts         = d._ts;
        if (d._lastActivityTime != null) dev.attributes.lastActivityTime = d._lastActivityTime;
        if (App.state.activeNodeId === deviceId) App.renderNodeInfo(dev);
      }
    } catch(x) { console.error('[ws]', x); }
  };
  ws.onclose = () => { ws = null; _reconnect(); };
  ws.onerror = () => {};
}

function _reconnect() {
  if (_reconnectTimer) return;
  if (!_currentDeviceId) return;
  _reconnectTimer = setTimeout(() => { _reconnectTimer = null; if (!ws && _currentDeviceId) connectWS(_currentDeviceId); }, 3000);
}

function _startSliding() {
  _stopSliding();
  _slide();
  _slideInterval = setInterval(_slide, 1000);
}

function _stopSliding() {
  if (_slideInterval) { clearInterval(_slideInterval); _slideInterval = null; }
}

async function startRealtimeCharts(deviceId, period) {
  _stopSliding();
  if (_reconnectTimer) { clearTimeout(_reconnectTimer); _reconnectTimer = null; }
  _currentDeviceId = deviceId;
  _chartKey = null;
  _currentPeriod = period || '1h';

  try { if (sensorChart) sensorChart.destroy(); } catch(e) {}
  try { if (rssiChart) rssiChart.destroy(); } catch(e) {}
  sensorChart = null; rssiChart = null;

  if (document.getElementById('chart-sensor')) {
    sensorChart = createChart('chart-sensor', '\uD83C\uDF31 Sensor', '#84cc16', false);
    const svKey = App.getNodeSensorVar({}, deviceId);
    if (svKey) { _chartKey = svKey; setChartSensor(svKey); _updateSensorHeader(svKey); }
  }
  if (document.getElementById('chart-rssi'))
    rssiChart = createChart('chart-rssi', 'RSSI', '#a78bfa', true);

  const hist = await fetchHistory(deviceId, _currentPeriod, _chartKey || 'temperature,humidity,light');
  if (hist.key) {
    _chartKey = hist.key;
    setChartSensor(hist.key);
    _updateSensorHeader(hist.key);
    App.setLiveVar(deviceId, hist.key);
  }
  loadPoints(sensorChart, hist.sensor);
  loadPoints(rssiChart, hist.rssi);

  _startSliding();
  connectWS(deviceId);
}
