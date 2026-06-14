/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · monitoreo.js
   Grafico superpuesto de todos los sensores + periodos de valvula
   ═══════════════════════════════════════════════════════════════════ */

let monitoreoChart = null;
let _monPeriod = '6h';
let _monMouseX = null;

const _monColors = {
  temperature: '#ea580c',
  humidity:    '#06b6d4',
  light:       '#fbbf24',
};

const _monLabels = {
  temperature: 'Temperatura',
  humidity:    'Humedad',
  light:       'Luminosidad',
};

const _monUnits = {
  temperature: '°C',
  humidity:    '%',
  light:       '%',
};

function _periodMsMon(p) {
  return p === '6h' ? 21600000 : 86400000;
}

function _tickFmtMon(v) {
  const d = new Date(v);
  if (_monPeriod === '24h')
    return d.toLocaleDateString('es-CO', { day: 'numeric', month: 'short', hour: '2-digit' });
  return d.toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' });
}

function _ttFmtMon(v) {
  return new Date(v).toLocaleString('es-CO', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
}

function _nearestPoint(dataset, xVal) {
  const pts = dataset.data;
  if (!pts || pts.length === 0) return null;
  let best = pts[0];
  let bestD = Math.abs(best.x - xVal);
  for (let i = 1; i < pts.length; i++) {
    const d = Math.abs(pts[i].x - xVal);
    if (d < bestD) { bestD = d; best = pts[i]; }
  }
  return best;
}

const valveOverlayPlugin = {
  id: 'valveOverlay',
  afterDraw(chart) {
    const periods = chart._valvePeriods;
    if (!periods || !periods.length) return;
    const ctx = chart.ctx;
    const xAxis = chart.scales.x;
    const yAxis = chart.scales.y;
    if (!xAxis || !yAxis) return;

    ctx.save();
    ctx.fillStyle = 'rgba(59, 130, 246, 0.18)';
    for (const p of periods) {
      const x1 = xAxis.getPixelForValue(p.start);
      const x2 = xAxis.getPixelForValue(p.end);
      if (x2 > xAxis.left && x1 < xAxis.right) {
        const cx1 = Math.max(x1, xAxis.left);
        const cx2 = Math.min(x2, xAxis.right);
        if (cx2 > cx1) ctx.fillRect(cx1, yAxis.top, cx2 - cx1, yAxis.bottom - yAxis.top);
      }
    }
    ctx.restore();
  }
};

const crosshairPlugin = {
  id: 'crosshair',
  afterInit(chart) {
    chart.canvas.addEventListener('mousemove', e => {
      const rect = chart.canvas.getBoundingClientRect();
      _monMouseX = e.clientX - rect.left;
      chart.draw();
    });
    chart.canvas.addEventListener('mouseleave', () => {
      _monMouseX = null;
      chart.draw();
    });
  },
  afterDraw(chart) {
    if (_monMouseX == null) return;
    const xAxis = chart.scales.x;
    const yAxis = chart.scales.y;
    if (!xAxis || !yAxis) return;
    const ctx = chart.ctx;
    const grid = App.cssVar('--border');
    const xVal = xAxis.getValueForPixel(_monMouseX);

    ctx.save();
    ctx.setLineDash([4, 4]);
    ctx.strokeStyle = App.cssVar('--text-muted');
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(_monMouseX, yAxis.top);
    ctx.lineTo(_monMouseX, yAxis.bottom);
    ctx.stroke();
    ctx.setLineDash([]);

    const tooltipEl = document.getElementById('monitoreo-tooltip');
    if (tooltipEl) {
      const lines = [`<span style="color:${App.cssVar('--text-muted')};font-size:0.78rem">${_ttFmtMon(xVal)}</span>`];
      chart.data.datasets.forEach(ds => {
        const pt = _nearestPoint(ds, xVal);
        if (pt) {
          const c = ds.borderColor;
          const u = _monUnits[Object.keys(_monUnits).find(k => _monLabels[k] === ds.label)] || '';
          lines.push(`<span style="color:${c}">\u25CF ${ds.label}: <strong>${parseFloat(pt.y).toFixed(1)}${u}</strong></span>`);
        }
      });
      tooltipEl.innerHTML = lines.join('<br>');
      const rect = chart.canvas.getBoundingClientRect();
      tooltipEl.style.left = (rect.left + _monMouseX + 12) + 'px';
      tooltipEl.style.top  = (rect.top + yAxis.top + 4) + 'px';
      tooltipEl.style.display = 'block';
    }

    chart.data.datasets.forEach(ds => {
      const pt = _nearestPoint(ds, xVal);
      if (!pt) return;
      const px = xAxis.getPixelForValue(pt.x);
      const py = yAxis.getPixelForValue(pt.y);
      ctx.beginPath();
      ctx.arc(px, py, 5, 0, Math.PI * 2);
      ctx.fillStyle = ds.borderColor;
      ctx.fill();
      ctx.strokeStyle = '#fff';
      ctx.lineWidth = 2;
      ctx.stroke();
    });

    ctx.restore();
  }
};

function _buildValvePeriods(points) {
  if (!points || points.length < 2) return [];
  const periods = [];
  let start = null;
  for (let i = 0; i < points.length; i++) {
    if (points[i].v === 1 && start === null) {
      start = points[i].ts;
    } else if ((points[i].v !== 1 || i === points.length - 1) && start !== null) {
      const end = points[i].v === 1 ? points[i].ts + 1000 : points[i].ts;
      periods.push({ start, end });
      start = null;
    }
  }
  if (start !== null) {
    const last = points[points.length - 1];
    periods.push({ start, end: last.ts + 1000 });
  }
  return periods;
}

async function _loadAllHistory(period) {
  const now = Date.now();
  const startTs = now - _periodMsMon(period);
  const interval = period === '6h' ? 60000 : 300000;
  let url = `/api/monitoreo/history?startTs=${startTs}&endTs=${now}&limit=4000&agg=AVG&interval=${interval}`;
  try {
    const res = await App.api(url);
    if (!res) return { sensors: {}, valve: [] };
    return await res.json();
  } catch(e) { console.error('[monitoreo]', e); return { sensors: {}, valve: [] }; }
}

async function initMonitoreo() {
  if (monitoreoChart) { monitoreoChart.destroy(); monitoreoChart = null; }

  document.querySelectorAll('.monitoreo-period-tab').forEach(tab => {
    tab.addEventListener('click', () => {
      document.querySelectorAll('.monitoreo-period-tab').forEach(t => t.classList.remove('active'));
      tab.classList.add('active');
      _monPeriod = tab.dataset.period;
      initMonitoreo();
    });
  });
  document.getElementById('btn-monitoreo-refresh')?.addEventListener('click', initMonitoreo);

  const grid = App.cssVar('--border');
  const tick = App.cssVar('--text-dim');
  const now  = Date.now();
  const w    = _periodMsMon(_monPeriod);

  const ctx = document.getElementById('monitoreo-chart');
  if (!ctx) return;

  const data = await _loadAllHistory(_monPeriod);
  const sensors = data.sensors || {};
  const valvePts = (data.valve || []).map(p => ({ ts: p.ts, v: parseFloat(p.value) })).filter(p => !isNaN(p.v));
  valvePts.sort((a, b) => a.ts - b.ts);

  const datasets = [];
  const sensorKeys = ['temperature', 'humidity', 'light'];
  let legendHtml = '';

  for (const key of sensorKeys) {
    const pts = (sensors[key] || []).map(p => ({ x: p.ts, y: parseFloat(p.value) }))
      .filter(p => !isNaN(p.y));
    pts.sort((a, b) => a.x - b.x);
    if (pts.length > 0) {
      datasets.push({
        label: _monLabels[key],
        data: pts,
        parsing: { xAxisKey: 'x', yAxisKey: 'y' },
        borderColor: _monColors[key],
        backgroundColor: 'transparent',
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.3,
      });
      legendHtml += `<span class="monitoreo-legend-item"><span class="monitoreo-legend-dot" style="background:${_monColors[key]}"></span>${_monLabels[key]} (${_monUnits[key]})</span>`;
    }
  }
  legendHtml += `<span class="monitoreo-legend-item"><span class="monitoreo-legend-dot" style="background:rgba(59,130,246,0.5)"></span>Valvula ON</span>`;

  document.getElementById('monitoreo-legend').innerHTML = legendHtml;

  monitoreoChart = new Chart(ctx, {
    type: 'line',
    data: { datasets },
    options: {
      responsive: true, maintainAspectRatio: false, animation: false,
      layout: { padding: { bottom: 8, left: 4, right: 8 } },
      interaction: { mode: 'x', intersect: false, axis: 'x' },
      hover: { mode: 'x', intersect: false, axis: 'x' },
      scales: {
        x: {
          type: 'linear',
          min: now - w, max: now,
          grid: { color: grid },
          ticks: { color: tick, font: { size: 10 }, maxTicksLimit: 8, callback: v => _tickFmtMon(v) },
        },
        y: {
          beginAtZero: true,
          grid: { color: grid },
          ticks: { color: tick, font: { size: 10 }, callback: v => parseFloat(v).toFixed(0) },
        },
      },
      plugins: {
        legend: { display: false },
        tooltip: { enabled: false },
      },
    },
    plugins: [valveOverlayPlugin, crosshairPlugin],
  });

  monitoreoChart._valvePeriods = _buildValvePeriods(valvePts);
  monitoreoChart.update('none');
}
