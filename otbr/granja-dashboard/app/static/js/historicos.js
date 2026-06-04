/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · historicos.js
   Consulta histórica con gráfico, tabla paginada y exportación CSV
   ═══════════════════════════════════════════════════════════════════ */

let histChart = null;
let histCurrentView = '24h';
let histAllData = [];
let histCurrentPage = 1;
const HIST_PAGE_SIZE = 100;

function initHistoricos() {
  document.querySelectorAll('.hist-tab').forEach(tab => {
    tab.addEventListener('click', () => {
      document.querySelectorAll('.hist-tab').forEach(t => t.classList.remove('active'));
      tab.classList.add('active');
      histCurrentView = tab.dataset.view;
      const customCtrls = document.getElementById('hist-custom-controls');
      if (customCtrls) customCtrls.style.display = histCurrentView === 'custom' ? 'flex' : 'none';
      loadHistoricos();
    });
  });

  document.getElementById('hist-device')?.addEventListener('change', loadHistoricos);
  document.getElementById('hist-variable')?.addEventListener('change', loadHistoricos);

  document.getElementById('btn-export-csv')?.addEventListener('click', exportCSV);

  loadHistoricos();
}

async function loadHistoricos() {
  const deviceId = document.getElementById('hist-device')?.value;
  const variable = document.getElementById('hist-variable')?.value;
  if (!deviceId || !variable) return;

  const nowx = Date.now();
  let startTs, endTs = nowx, interval = 3600000;

  if (histCurrentView === '24h') {
    startTs = nowx - 86400000;
    interval = 600000; // 10 min
  } else if (histCurrentView === '30d') {
    startTs = nowx - 30 * 86400000;
    interval = 7200000; // 2h
  } else {
    const startEl = document.getElementById('hist-start');
    const endEl = document.getElementById('hist-end');
    const intervalEl = document.getElementById('hist-interval');
    if (startEl && endEl) {
      startTs = new Date(startEl.value + 'T00:00:00').getTime();
      endTs = new Date(endEl.value + 'T23:59:59').getTime();
    }
    if (intervalEl) interval = parseInt(intervalEl.value);
  }

  const url = `/api/telemetry/${deviceId}/history?keys=${variable}&startTs=${startTs}&endTs=${endTs}&agg=AVG&interval=${interval}`;

  try {
    const res = await App.api(url);
    if (!res) return;
    const data = await res.json();

    histAllData = (data[variable] || []).map(pt => {
      const agg = pt.aggValues || {};
      return {
        ts: pt.ts,
        value: parseFloat(pt.value),
        avg: agg.AVG !== undefined ? parseFloat(agg.AVG) : parseFloat(pt.value),
        min: agg.MIN !== undefined ? parseFloat(agg.MIN) : (pt.min !== undefined ? parseFloat(pt.min) : parseFloat(pt.value)),
        max: agg.MAX !== undefined ? parseFloat(agg.MAX) : (pt.max !== undefined ? parseFloat(pt.max) : parseFloat(pt.value)),
      };
    });

    updateHistSummary(histAllData, variable);
    updateHistChart(histAllData, variable, histCurrentView);
    histCurrentPage = 1;
    renderHistPage();
  } catch(e) {
    console.error('[historicos]', e);
    App.toast('Error al cargar históricos', 'error');
  }
}

function updateHistSummary(data, variable) {
  const container = document.getElementById('hist-summary');
  if (!container) return;
  if (data.length === 0) { container.innerHTML = ''; return; }

  const values = data.map(d => d.value).filter(v => !isNaN(v));
  if (values.length === 0) { container.innerHTML = ''; return; }

  const avg = (values.reduce((a, b) => a + b, 0) / values.length).toFixed(1);
  const min = Math.min(...values).toFixed(1);
  const max = Math.max(...values).toFixed(1);

  const unit = variable === 'temperature' ? '°C' : variable === 'humidity' ? '%' : 'mV';

  container.innerHTML = `
    <div class="summary-card"><div class="summary-label">Promedio</div><div class="summary-value">${avg} ${unit}</div></div>
    <div class="summary-card" style="color:var(--water)"><div class="summary-label">Mínimo</div><div class="summary-value">${min} ${unit}</div></div>
    <div class="summary-card" style="color:var(--accent)"><div class="summary-label">Máximo</div><div class="summary-value">${max} ${unit}</div></div>
    <div class="summary-card"><div class="summary-label">Registros</div><div class="summary-value">${data.length}</div></div>`;
}

function updateHistChart(data, variable, view) {
  const canvas = document.getElementById('hist-chart');
  if (!canvas) return;
  if (histChart) histChart.destroy();

  const gridColor = getComputedStyle(document.documentElement).getPropertyValue('--border').trim();
  const tickColor = getComputedStyle(document.documentElement).getPropertyValue('--text-dim').trim();
  const textColor = getComputedStyle(document.documentElement).getPropertyValue('--text').trim();
  const cardBg = getComputedStyle(document.documentElement).getPropertyValue('--card').trim();

  const labels = data.map(pt => new Date(pt.ts).toLocaleString('es-CO', {
    month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit',
  }));
  const values = data.map(pt => pt.value);

  const chartLabel = variable === 'temperature' ? 'Temperatura (°C)' : variable === 'humidity' ? 'Humedad (%)' : 'Batería (mV)';
  const color = variable === 'temperature' ? '#f59e0b' : variable === 'humidity' ? '#06b6d4' : '#22c55e';

  histChart = new Chart(canvas, {
    type: 'line',
    data: {
      labels,
      datasets: [{
        label: chartLabel,
        data: values,
        borderColor: color,
        backgroundColor: color + '18',
        borderWidth: 2,
        pointRadius: view === '30d' ? 0 : 2,
        pointHoverRadius: 5,
        fill: true,
        tension: 0.3,
      }],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 300 },
      interaction: { intersect: false, mode: 'index' },
      scales: {
        x: {
          ticks: { color: tickColor, maxTicksLimit: 10, maxRotation: 45, font: { size: 10 } },
          grid: { color: gridColor },
        },
        y: {
          ticks: { color: tickColor, font: { size: 10 } },
          grid: { color: gridColor },
        },
      },
      plugins: {
        legend: { labels: { color: textColor, font: { size: 11 } } },
        tooltip: { backgroundColor: cardBg, titleColor: textColor, bodyColor: textColor, borderColor: gridColor, borderWidth: 1 },
      },
    },
  });
}

function renderHistPage() {
  const tbody = document.getElementById('hist-table-body');
  const pag = document.getElementById('hist-pagination');
  if (!tbody) return;

  const total = histAllData.length;
  const totalPages = Math.ceil(total / HIST_PAGE_SIZE);
  const start = (histCurrentPage - 1) * HIST_PAGE_SIZE;
  const pageData = histAllData.slice(start, start + HIST_PAGE_SIZE);

  tbody.innerHTML = '';
  if (pageData.length === 0) {
    tbody.innerHTML = '<tr><td colspan="4" style="text-align:center;padding:32px;color:var(--text-muted)">Sin datos para este período</td></tr>';
    if (pag) pag.innerHTML = '';
    return;
  }

  pageData.forEach(pt => {
    const date = new Date(pt.ts).toLocaleString('es-CO', {
      year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit',
    });
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${date}</td>
      <td class="td-avg">${pt.avg.toFixed(1)}</td>
      <td class="td-min">${pt.min.toFixed(1)}</td>
      <td class="td-max">${pt.max.toFixed(1)}</td>`;
    tbody.appendChild(tr);
  });

  if (pag && totalPages > 1) {
    let btns = `<span>${total} registros · Pág ${histCurrentPage}/${totalPages}</span><div class="page-btns">`;
    btns += `<button ${histCurrentPage === 1 ? 'disabled' : ''} onclick="histGoTo(1)">««</button>`;
    btns += `<button ${histCurrentPage === 1 ? 'disabled' : ''} onclick="histGoTo(${histCurrentPage - 1})">«</button>`;
    for (let i = Math.max(1, histCurrentPage - 2); i <= Math.min(totalPages, histCurrentPage + 2); i++) {
      btns += `<button class="${i === histCurrentPage ? 'active' : ''}" onclick="histGoTo(${i})">${i}</button>`;
    }
    btns += `<button ${histCurrentPage === totalPages ? 'disabled' : ''} onclick="histGoTo(${histCurrentPage + 1})">»</button>`;
    btns += `<button ${histCurrentPage === totalPages ? 'disabled' : ''} onclick="histGoTo(${totalPages})">»»</button>`;
    btns += '</div>';
    pag.innerHTML = btns;
  } else if (pag) {
    pag.innerHTML = `<span>${total} registros</span>`;
  }
}

function histGoTo(page) {
  histCurrentPage = page;
  renderHistPage();
  document.querySelector('.hist-table-container')?.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

function exportCSV() {
  if (histAllData.length === 0) {
    App.toast('No hay datos para exportar', 'warning');
    return;
  }
  const variable = document.getElementById('hist-variable')?.value || 'variable';
  const deviceName = document.getElementById('hist-device')?.selectedOptions[0]?.text || 'dispositivo';

  let csv = 'Fecha,Promedio,Mínimo,Máximo\n';
  histAllData.forEach(pt => {
    const date = new Date(pt.ts).toISOString();
    csv += `${date},${pt.avg.toFixed(1)},${pt.min.toFixed(1)},${pt.max.toFixed(1)}\n`;
  });

  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `granja_${variable}_${deviceName.replace(/\s+/g,'_')}.csv`;
  a.click();
  URL.revokeObjectURL(url);
  App.toast('Archivo CSV descargado', 'success');
}