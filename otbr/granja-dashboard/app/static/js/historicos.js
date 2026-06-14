/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · historicos.js
   Consulta historica con grafico, tabla paginada y exportacion CSV
   ═══════════════════════════════════════════════════════════════════ */

let histChart = null;
let histAllData = [];
let histCurrentPage = 1;
const HIST_PAGE_SIZE = 100;

function initHistoricos() {
  document.getElementById('hist-device')?.addEventListener('change', () => {
    autoSelectHistVariable();
    loadHistoricos();
  });
  document.getElementById('hist-variable')?.addEventListener('change', loadHistoricos);
  document.getElementById('btn-export-csv')?.addEventListener('click', exportCSV);

  autoSelectHistVariable();
  loadHistoricos();
}

function autoSelectHistVariable() {
  const deviceId = document.getElementById('hist-device')?.value;
  const varSel = document.getElementById('hist-variable');
  if (!deviceId || !varSel) return;

  const dev = (App.state?.devices || []).find(d => d.id === deviceId);
  if (!dev) return;

  const tel = dev.telemetry || {};
  const sensorVar = App.getNodeSensorVar(tel, deviceId);
  if (sensorVar && App.SENSOR_VARS[sensorVar]) {
    varSel.value = sensorVar;
  }

  if (varSel.closest('.custom-select')) {
    updateCustomSelectDisplay(varSel);
  }
}

function updateCustomSelectDisplay(sel) {
  const wrapper = sel.closest('.custom-select');
  if (!wrapper) return;
  const label = wrapper.querySelector('.cs-label');
  if (!label) return;
  const opt = sel.options[sel.selectedIndex];
  if (opt) label.textContent = opt.textContent;
  const options = wrapper.querySelectorAll('.cs-option');
  options.forEach(o => {
    const val = o.dataset.value;
    o.classList.toggle('selected', val === sel.value);
  });
}

async function loadHistoricos() {
  const deviceId = document.getElementById('hist-device')?.value;
  const variable = document.getElementById('hist-variable')?.value;
  if (!deviceId || !variable) return;

  const startEl = document.getElementById('hist-start');
  const endEl = document.getElementById('hist-end');
  const intervalEl = document.getElementById('hist-interval');
  if (!startEl || !endEl) return;

  const startTs = new Date(startEl.value + 'T00:00:00').getTime();
  const endTs = new Date(endEl.value + 'T23:59:59').getTime();
  const interval = intervalEl ? parseInt(intervalEl.value) : 0;

  let url = `/api/telemetry/${deviceId}/history?keys=${variable}&startTs=${startTs}&endTs=${endTs}`;
  if (interval > 0) url += `&agg=AVG&interval=${interval}`;

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
    histAllData.sort((a, b) => a.ts - b.ts);

    updateHistSummary(histAllData, variable);
    updateHistChart(histAllData, variable, endTs - startTs > 7 * 86400000 ? '30d' : '24h');
    histCurrentPage = 1;
    renderHistPage();
  } catch(e) {
    console.error('[historicos]', e);
    App.toast('Error al cargar historicos', 'error');
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

  const unit = variable === 'temperature' ? '\u00B0C' : variable === 'humidity' ? '%' : variable === 'light' ? '%' : 'mV';

  container.innerHTML = `
    <div class="summary-card"><div class="summary-label">Promedio</div><div class="summary-value">${avg} ${unit}</div></div>
    <div class="summary-card" style="color:var(--water)"><div class="summary-label">Minimo</div><div class="summary-value">${min} ${unit}</div></div>
    <div class="summary-card" style="color:var(--accent)"><div class="summary-label">Maximo</div><div class="summary-value">${max} ${unit}</div></div>
    <div class="summary-card"><div class="summary-label">Registros</div><div class="summary-value">${data.length}</div></div>`;
}

function updateHistChart(data, variable, view) {
  const canvas = document.getElementById('hist-chart');
  if (!canvas) return;
  if (histChart) histChart.destroy();

  const gridColor = App.cssVar('--border');
  const tickColor = App.cssVar('--text-dim');
  const textColor = App.cssVar('--text');
  const cardBg = App.cssVar('--card');

  const labels = data.map(pt => new Date(pt.ts).toLocaleString('es-CO', {
    month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit',
  }));
  const values = data.map(pt => pt.value);

  const chartLabel = variable === 'temperature' ? 'Temperatura (\u00B0C)' : variable === 'humidity' ? 'Humedad (%)' : variable === 'light' ? 'Luminosidad (%)' : 'Bateria (mV)';
  const color = variable === 'temperature' ? '#f59e0b' : variable === 'humidity' ? '#06b6d4' : variable === 'light' ? '#fbbf24' : '#22c55e';

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
      animation: false,
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
    tbody.innerHTML = '<tr><td colspan="4" style="text-align:center;padding:32px;color:var(--text-muted)">Sin datos para este periodo</td></tr>';
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
    let btns = `<span>${total} registros \u00B7 Pag ${histCurrentPage}/${totalPages}</span><div class="page-btns">`;
    btns += `<button ${histCurrentPage === 1 ? 'disabled' : ''} onclick="histGoTo(1)">\u00AB\u00AB</button>`;
    btns += `<button ${histCurrentPage === 1 ? 'disabled' : ''} onclick="histGoTo(${histCurrentPage - 1})">\u00AB</button>`;
    for (let i = Math.max(1, histCurrentPage - 2); i <= Math.min(totalPages, histCurrentPage + 2); i++) {
      btns += `<button class="${i === histCurrentPage ? 'active' : ''}" onclick="histGoTo(${i})">${i}</button>`;
    }
    btns += `<button ${histCurrentPage === totalPages ? 'disabled' : ''} onclick="histGoTo(${histCurrentPage + 1})">\u00BB</button>`;
    btns += `<button ${histCurrentPage === totalPages ? 'disabled' : ''} onclick="histGoTo(${totalPages})">\u00BB\u00BB</button>`;
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

  let csv = 'Fecha,Promedio,Minimo,Maximo\n';
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
