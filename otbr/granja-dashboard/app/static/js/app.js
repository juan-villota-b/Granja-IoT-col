/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · app.js
   Core: estado, API, router, toasts, utilidades
   ═══════════════════════════════════════════════════════════════════ */

const App = (() => {

  // ── Estado centralizado ──────────────────────────────────────────
  const state = {
    devices: [],
    edges: [],
    activeNodeId: null,
    currentPage: 'dashboard',
    authority: '',
    displayName: '',
  };

  const _liveVar = {}; // deviceId -> 'temperature'|'humidity'|'light'

  // ── Sanitizacion ─────────────────────────────────────────────────
  function esc(str) {
    if (str === null || str === undefined) return '';
    const s = String(str);
    const div = document.createElement('div');
    div.textContent = s;
    return div.innerHTML;
  }

  // ── CSS variable helper ──────────────────────────────────────────
  function cssVar(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }

  // ── API helper ───────────────────────────────────────────────────
  async function api(endpoint, options = {}) {
    try {
      const res = await fetch(endpoint, {
        credentials: 'include',
        ...options,
        headers: { 'Content-Type': 'application/json', ...options.headers },
      });
      if (res.status === 401 && !window.location.pathname.startsWith('/login')) {
        window.location.href = '/login';
        return null;
      }
      return res;
    } catch (err) {
      console.error('[api]', endpoint, err);
      toast('Error de conexion', 'error');
      return null;
    }
  }

  // ── Toast system ─────────────────────────────────────────────────
  function toast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    if (!container) return;
    const el = document.createElement('div');
    el.className = `toast ${type}`;
    const icons = { success: '\u2713', error: '\u2715', info: '\u2139', warning: '\u26A0' };
    el.textContent = `${icons[type] || ''} ${message}`;
    container.appendChild(el);
    setTimeout(() => { if (el.parentNode) el.remove(); }, 4200);
  }

  // ── Utilidades ───────────────────────────────────────────────────
  function formatUptime(seconds) {
    const s = parseInt(seconds);
    if (isNaN(s) || s <= 0) return '--';
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (d > 0) return `${d}d ${h}h`;
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m`;
  }

  // ── Carga de perfil ─────────────────────────────────────────────
  async function loadMe() {
    const res = await api('/api/me');
    if (!res) return;
    const data = await res.json();
    state.authority = data.authority;
    state.displayName = data.display_name;
    const userNameEl = document.getElementById('user-name');
    const userAvatarEl = document.getElementById('user-avatar');
    if (userNameEl) userNameEl.textContent = state.displayName;
    if (userAvatarEl) userAvatarEl.textContent = state.displayName.charAt(0).toUpperCase();

    if (data.authority === 'CUSTOMER_USER') {
      state.isCustomer = true;
    }
  }

  // ── Carga de edges ──────────────────────────────────────────────
  async function loadEdges() {
    const res = await api('/api/edges');
    if (!res) return;
    const data = await res.json();
    state.edges = data.edges || [];

    const sel = document.getElementById('edge-selector');
    if (!sel) return;
    const wrapper = sel.closest('.custom-select');
    if (wrapper) {
      wrapper.parentNode.insertBefore(sel, wrapper);
      wrapper.remove();
    }

    sel.innerHTML = '';
    if (state.edges.length === 0) {
      sel.innerHTML = '<option value="">Sin edges asignados</option>';
    } else {
      state.edges.forEach(e => {
        const opt = document.createElement('option');
        opt.value = e.id;
        opt.textContent = esc(e.name);
        sel.appendChild(opt);
      });
    }
    initCustomSelects(sel.parentElement || document);

    if (state.isCustomer && state.edges.length === 1) {
      sel.value = state.edges[0].id;
      sel.disabled = true;
      sel.style.display = 'none';
      const edgeContainer = document.getElementById('edge-selector-container');
      const label = edgeContainer?.querySelector('.nav-label');
      if (label) label.textContent = 'Finca';
    }
  }

  // ── Carga de dispositivos ────────────────────────────────────────
  async function loadDevices() {
    const res = await api('/api/devices');
    if (!res) return;
    const data = await res.json();
    state.devices = data.devices || [];

    const selector = document.getElementById('node-selector');
    if (selector) {
      const wrapper = selector.closest('.custom-select');
      if (wrapper) {
        wrapper.parentNode.insertBefore(selector, wrapper);
        wrapper.remove();
      }
      selector.innerHTML = '<option value="">Selecciona un nodo...</option>';
      state.devices.forEach(d => {
        const opt = document.createElement('option');
        opt.value = d.id;
        opt.textContent = `${esc(d.name)} (${esc(d.type)})`;
        selector.appendChild(opt);
      });
      selector.addEventListener('change', e => {
        if (e.target.value) selectNode(e.target.value);
      });
      initCustomSelects(selector.parentElement || document);
    }

    const badge = document.getElementById('badge-active');
    if (badge) {
      const active = state.devices.filter(d => isDeviceActive(d)).length;
      badge.textContent = active;
      badge.style.display = active > 0 ? '' : 'none';
    }
  }

  // ── Seleccion de nodo ────────────────────────────────────────────
  let _chartPeriod = '1h';
  function selectNode(deviceId, period) {
    const dev = state.devices.find(d => d.id === deviceId);
    if (!dev) return;
    state.activeNodeId = deviceId;
    if (period) _chartPeriod = period;

    const tel = dev.telemetry || {};
    const sensorVar = getNodeSensorVar(tel, deviceId);
    const sv = sensorVar ? SENSOR_VARS[sensorVar] : null;

    const chartsContainer = document.getElementById('panel-charts');
    if (chartsContainer) {
      const periodTabs = ['1h','6h','24h'].map(p =>
        `<button class="chart-period-tab${p === _chartPeriod ? ' active' : ''}" data-period="${p}">${p}</button>`
      ).join('');
      chartsContainer.innerHTML = `
        <div class="chart-period-bar">${periodTabs}</div>
        <div class="chart-container chart-primary">
          <h4>${sv ? `<span style="color:${sv.color}">${sv.icon}</span> ${sv.label}` : '\uD83C\uDF31 Sensor'}</h4>
          <canvas id="chart-sensor"></canvas>
        </div>
        <div class="chart-container chart-rssi">
          <h4>\uD83D\uDCE1 RSSI</h4>
          <canvas id="chart-rssi"></canvas>
        </div>`;
      chartsContainer.querySelectorAll('.chart-period-tab').forEach(tab => {
        tab.addEventListener('click', () => {
          const p = tab.dataset.period;
          selectNode(deviceId, p);
        });
      });
    }

    renderNodeInfo(dev);
    loadNodeMiniHistory(deviceId);
    if (typeof startRealtimeCharts === 'function') startRealtimeCharts(deviceId, _chartPeriod);
    if (typeof updateMapHighlight === 'function') updateMapHighlight(deviceId);
    document.getElementById('node-selector').value = deviceId;
  }

  function renderNodeInfo(dev) {
    const info = document.getElementById('node-info');
    if (!info) return;
    const tel = dev.telemetry || {};
    const active = isDeviceActive(dev);
    const sensorVar = getNodeSensorVar(tel, dev.id);
    const sv = sensorVar ? SENSOR_VARS[sensorVar] : null;

    let cards = '';
    if (sv) {
      cards += `<div class="info-card info-card-primary">
        <div class="info-label">${sv.icon} ${sv.label}</div>
        <div class="info-value" style="color:${sv.color}">${esc(tel[sensorVar])} <small>${sv.unit}</small></div>
      </div>`;
    }
    cards += `<div class="info-card info-card-rssi">
      <div class="info-label">\uD83D\uDCE1 RSSI</div>
      <div class="info-value">${esc(tel.rssi)} <small>dBm</small></div>
    </div>`;

    info.innerHTML = `
      <div class="node-info-header">
        <div class="node-indicator${active ? '' : ' inactive'}" style="background:${active ? 'var(--success)' : 'var(--danger)'}"></div>
        <span class="node-info-title">${esc(dev.name)}</span>
        <span class="node-info-type">${esc(dev.type)}</span>
        <button class="btn-delete-node" title="Eliminar nodo" onclick="App.deleteNode('${esc(dev.id)}','${esc(dev.name)}')">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>
          </svg>
        </button>
      </div>
      <div class="node-info-grid two-card">${cards}</div>`;
  }

  // ── Eliminar nodo ───────────────────────────────────────────────
  async function deleteNode(deviceId, deviceName) {
    if (!confirm(`Eliminar "${deviceName}"?\n\nEsta accion borra el dispositivo de ThingsBoard y no se puede deshacer.`)) return;

    const res = await api(`/api/devices/${deviceId}`, { method: 'DELETE' });
    if (!res || !res.ok) {
      toast('Error al eliminar el nodo', 'error');
      return;
    }

    state.devices = state.devices.filter(d => d.id !== deviceId);
    if (state.activeNodeId === deviceId) {
      state.activeNodeId = null;
      const info = document.getElementById('node-info');
      if (info) info.innerHTML = `<p class="placeholder-text">
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" style="opacity:0.3;display:block;margin:0 auto 12px">
          <rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>
        </svg>Selecciona un nodo en el mapa<br>o en el selector para ver su telemetria</p>`;
      const charts = document.getElementById('panel-charts');
      if (charts) charts.innerHTML = '';
      const mini = document.getElementById('node-history-mini');
      if (mini) mini.innerHTML = '';
      document.getElementById('node-selector').value = '';
    }

    loadDevices().then(() => {
      if (typeof initMap === 'function') initMap(state.devices);
    });

    if (typeof updateMapHighlight === 'function') {
      Object.values(markers || {}).forEach(m => m.setZIndexOffset(0));
    }

    toast(`Nodo "${deviceName}" eliminado`, 'success');
  }

  // ── Mini historico al clickear nodo ──────────────────────────────
  let _miniChart = null;

  async function loadNodeMiniHistory(deviceId) {
    const container = document.getElementById('node-history-mini');
    if (!container) return;

    const dev = state.devices.find(d => d.id === deviceId);
    if (!dev) return;
    const tel = dev.telemetry || {};
    const sensorVar = getNodeSensorVar(tel, deviceId);
    if (!sensorVar) { container.innerHTML = ''; return; }
    const sv = SENSOR_VARS[sensorVar];
    const now = Date.now();
    const startTs = now - 86400000;

    const url = `/api/telemetry/${deviceId}/history?keys=${sensorVar}&startTs=${startTs}&endTs=${now}&agg=AVG&interval=600000&limit=200`;
    try {
      const res = await api(url);
      if (!res) { container.innerHTML = ''; return; }
      const data = await res.json();
      const points = (data[sensorVar] || []).map(p => ({ ts: p.ts, v: parseFloat(p.value) })).filter(p => !isNaN(p.v));
      if (points.length === 0) { container.innerHTML = ''; return; }

      const gridColor = cssVar('--border');
      const tickColor = cssVar('--text-dim');
      const labels = points.map(p => {
        const d = new Date(p.ts);
        if (points.length > 100) return d.toLocaleDateString('es-CO', { month: 'short', day: 'numeric' });
        return d.toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' });
      });
      const values = points.map(p => p.v);
      const avg = (values.reduce((a, b) => a + b, 0) / values.length).toFixed(1);
      const min = Math.min(...values).toFixed(1);
      const max = Math.max(...values).toFixed(1);

      container.innerHTML = `
        <div class="mini-history-header">
          <span>${sv.icon} 24h</span>
          <span class="mini-history-stats"><span style="color:${sv.color}">Avg ${avg}${sv.unit}</span> &middot; Min ${min} &middot; Max ${max}</span>
        </div>
        <div class="mini-history-chart"><canvas id="mini-history-canvas"></canvas></div>`;

      if (_miniChart) _miniChart.destroy();
      const ctx = document.getElementById('mini-history-canvas');
      if (!ctx) return;
      _miniChart = new Chart(ctx, {
        type: 'line',
        data: {
          labels,
          datasets: [{
            data: values,
            borderColor: sv.color,
            backgroundColor: sv.color + '22',
            borderWidth: 1.5,
            pointRadius: 0,
            fill: true,
            tension: 0.3,
          }],
        },
        options: {
          responsive: true, maintainAspectRatio: false, animation: { duration: 300 },
          interaction: { intersect: false, mode: 'index' },
          scales: {
            x: { display: true, grid: { display: false }, ticks: { color: tickColor, font: { size: 7 }, maxTicksLimit: 6, maxRotation: 45 } },
            y: { display: true, grid: { color: gridColor }, ticks: { color: tickColor, font: { size: 8 }, maxTicksLimit: 3, callback: v => parseFloat(v).toFixed(1) } },
          },
          plugins: { legend: { display: false }, tooltip: { enabled: true, backgroundColor: cssVar('--surface'), titleColor: cssVar('--text'), bodyColor: sv.color, borderColor: gridColor, borderWidth: 1 } },
        },
      });
    } catch (e) {
      console.error('[mini-hist]', e);
      container.innerHTML = '';
    }
  }

  // ── Sensor variable detection ────────────────────────────────────
  function getNodeSensorVar(tel, deviceId) {
    const dev = deviceId ? state.devices.find(d => d.id === deviceId) : null;
    const sensorType = dev?.attributes?.sensor_type;
    if (sensorType && (sensorType === 'temperature' || sensorType === 'humidity' || sensorType === 'light' || sensorType === 'valve')) return sensorType;
    let check = _detectFromTelemetry(tel);
    if (check) return check;
    if (deviceId && _liveVar[deviceId]) return _liveVar[deviceId];
    return null;
  }

  function _detectFromTelemetry(tel) {
    if (!tel) return null;
    const keys = ['temperature', 'humidity', 'light', 'valve', 'battery'];
    for (const k of keys) {
      const v = tel[k];
      if (v === undefined || v === null) continue;
      if (Array.isArray(v) && v.length === 0) continue;
      return k;
    }
    return null;
  }

  function setLiveVar(deviceId, sensorKey) {
    if (sensorKey) _liveVar[deviceId] = sensorKey;
  }

  const SENSOR_VARS = {
    temperature: { label: 'Temperatura', unit: '\u00B0C', color: '#f59e0b', icon: '\uD83C\uDF21\uFE0F' },
    humidity:    { label: 'Humedad', unit: '%', color: '#06b6d4', icon: '\uD83D\uDCA7' },
    light:       { label: 'Luminosidad', unit: '%',   color: '#fbbf24', icon: '\u2600\uFE0F' },
    valve:       { label: 'Valvula', unit: '',   color: '#22c55e', icon: '\uD83D\uDCA7' },
  };

  function isDeviceActive(dev) {
    const tel = dev.telemetry || {};
    const hasData = (tel.temperature !== undefined && tel.temperature !== null)
                 || (tel.humidity !== undefined && tel.humidity !== null)
                 || (tel.light !== undefined && tel.light !== null)
                 || (tel.valve !== undefined && tel.valve !== null)
                 || (tel.battery !== undefined && tel.battery !== null);
    if (!hasData) return false;

    const attrs = dev.attributes || {};
    const now = Date.now();
    const TIMEOUT = 600000; // 10 min

    const lastActivity = parseInt(attrs.lastActivityTime);
    if (!isNaN(lastActivity) && lastActivity > 0) return (now - lastActivity) < TIMEOUT;

    const ts = parseInt(tel._ts);
    if (!isNaN(ts) && ts > 0) return (now - ts) < TIMEOUT;

    return true;
  }

  // ── Helper: color del marcador por tipo ──────────────────────────
  function markerColor(active, isValve, isSensor) {
    if (!active) return '#6b7280';
    if (isValve) return '#ef4444';
    if (isSensor) return '#22c55e';
    return '#3b82f6';
  }

  // ── SPA Router ───────────────────────────────────────────────────
  function navigate(page) {
    state.currentPage = page;
    const main = document.getElementById('main-content');
    const isMobile = window.matchMedia('(max-width: 767px)').matches;

    if (typeof ws !== 'undefined' && ws) { try { ws.close(); } catch(e) {} }
    if (typeof valveCheckInterval !== 'undefined' && valveCheckInterval) { clearInterval(valveCheckInterval); valveCheckInterval = null; }

    main.classList.add('fading');
    setTimeout(() => {
      switch (page) {
        case 'dashboard':
          main.style.overflow = isMobile ? 'auto' : 'hidden';
          main.innerHTML = dashboardMarkup();
          initDashboard();
          break;
        case 'historicos':
          main.style.overflow = 'auto';
          main.innerHTML = historicosMarkup();
          initHistoricosPage();
          break;
        case 'valvula':
          main.style.overflow = 'auto';
          main.innerHTML = valvulaMarkup();
          if (typeof initValvula === 'function') initValvula();
          break;
        case 'monitoreo':
          main.style.overflow = 'auto';
          main.innerHTML = monitoreoMarkup();
          if (typeof initMonitoreo === 'function') initMonitoreo();
          break;
        case 'add-node':
          main.style.overflow = 'auto';
          main.innerHTML = addNodeMarkup();
          setTimeout(() => initAddNode(), 100);
          break;
      }
      main.classList.remove('fading');
      main.classList.add('fade-in');
      setTimeout(() => {
        main.classList.remove('fade-in');
        initCustomSelects();
      }, 300);
    }, 150);

    document.querySelectorAll('.nav-link').forEach(n => n.classList.remove('active'));
    const activeLink = document.querySelector(`[data-page="${page}"]`);
    if (activeLink) activeLink.classList.add('active');

    document.querySelectorAll('.bottom-nav-btn').forEach(b => b.classList.remove('active'));
    const activeBottom = document.querySelector(`.bottom-nav-btn[data-page="${page}"]`);
    if (activeBottom) activeBottom.classList.add('active');
  }

  // ── Inicializacion Dashboard ─────────────────────────────────────
  async function initDashboard() {
    document.getElementById('main-content').style.overflow = 'hidden';
    await loadDevices();
    if (typeof initMap === 'function') initMap(state.devices);
    if (state.activeNodeId) {
      document.getElementById('node-selector').value = state.activeNodeId;
    }
  }

  // ── Markup: Dashboard ────────────────────────────────────────────
  function dashboardMarkup() {
    return `<div class="dashboard-layout">
        <div class="dashboard-map" id="dashboard-map">
            <div class="map-status-bar" id="status-bar"></div>
            <div class="zoom-hint" id="zoom-hint">Acerca el zoom para ver los sensores</div>
        </div>
        <div class="dashboard-panel" id="dashboard-panel">
            <div class="panel-header">
                <select id="node-selector" class="customizable">
                    <option value="">Selecciona un nodo...</option>
                </select>
            </div>
            <div class="panel-body">
                <div id="node-info" class="node-info">
                    <p class="placeholder-text">
                        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" style="opacity:0.3;display:block;margin:0 auto 12px">
                            <rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>
                        </svg>
                        Selecciona un nodo en el mapa<br>o en el selector para ver su telemetria
                    </p>
                </div>
                <div id="node-history-mini" class="node-history-mini"></div>
                <div class="panel-charts" id="panel-charts"></div>
            </div>
        </div>
    </div>`;
  }

  // ── Markup: Historicos ───────────────────────────────────────────
  function historicosMarkup() {
    const now = new Date();
    const endISO = now.toISOString().slice(0, 10);
    const startISO = new Date(now - 7 * 86400000).toISOString().slice(0, 10);
    return `<div class="historicos-container">
        <div class="page-header">
            <h2>
                <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/>
                </svg>
                Historicos
            </h2>
            <button class="btn-sm outline" id="btn-export-csv" title="Exportar a CSV">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                Exportar CSV
            </button>
        </div>
        <div class="hist-controls">
            <select id="hist-device" class="customizable cs-sm"><option value="">Cargando...</option></select>
            <select id="hist-variable" class="customizable cs-sm">
                <option value="temperature">Temperatura</option>
                <option value="humidity">Humedad</option>
                <option value="light">Luminosidad</option>
                <option value="valve">Valvula</option>
                <option value="battery">Bateria</option>
            </select>
            <input type="date" id="hist-start" value="${startISO}" />
            <input type="date" id="hist-end" value="${endISO}" />
            <select id="hist-interval" class="customizable cs-sm">
                <option value="0" selected>Sin agregacion</option>
                <option value="60000">1 minuto</option>
                <option value="300000">5 minutos</option>
                <option value="3600000">1 hora</option>
                <option value="21600000">6 horas</option>
                <option value="43200000">12 horas</option>
                <option value="86400000">24 horas</option>
            </select>
            <button class="btn-sm" onclick="App.loadHistoricos()">Consultar</button>
        </div>
        <div id="hist-summary" class="hist-summary"></div>
        <div class="hist-chart-container"><canvas id="hist-chart"></canvas></div>
        <div class="hist-table-container">
            <table class="hist-table">
                <thead><tr><th>Fecha/Hora</th><th>Promedio</th><th>Minimo</th><th>Maximo</th></tr></thead>
                <tbody id="hist-table-body"></tbody>
            </table>
            <div class="hist-pagination" id="hist-pagination"></div>
        </div>
    </div>`;
  }

  // ── Markup: Valvula ──────────────────────────────────────────────
  function valvulaMarkup() {
    return `<div class="valvula-container">
        <div class="page-header">
            <h2>
                <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M12 22c1-2 2-4 2-7 0-3-2-5-2-7"/><path d="M12 22c-1-2-2-4-2-7 0-3 2-5 2-7"/><path d="M6 15c3 3 9 3 12 0"/><path d="M4 7c0 4 3 7 8 10"/><path d="M20 7c0 4-3 7-8 10"/>
                </svg>
                Control de Valvula
            </h2>
        </div>
        <div class="valvula-selector">
            <label>Nodo actuador</label>
            <select id="valve-device-select" class="customizable cs-sm">
                <option value="">Cargando...</option>
            </select>
        </div>
        <div class="valvula-status-card">
            <div class="valvula-status-indicator" id="valve-status-indicator">
                <div class="valvula-state-dot" style="background:var(--danger)"></div>
                <span id="valve-status-text">Desconectada</span>
            </div>
            <p class="valvula-device-name" id="valve-device-name">Esperando nodo valvula...</p>
        </div>
        <div class="valvula-btn-container">
            <button class="btn-valve btn-valve-open" id="btn-valve-on" disabled>
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><path d="M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z"/></svg>
                ABRIR VALVULA
            </button>
            <button class="btn-valve btn-valve-close" id="btn-valve-off" disabled>
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                CERRAR VALVULA
            </button>
        </div>
        <p class="valve-feedback" id="valve-feedback"></p>
    </div>`;
  }

  // ── Markup: Monitoreo ──────────────────────────────────────────
  function monitoreoMarkup() {
    return `<div class="monitoreo-container">
        <div class="page-header">
            <h2>
                <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <line x1="18" y1="20" x2="18" y2="10"/><line x1="12" y1="20" x2="12" y2="4"/><line x1="6" y1="20" x2="6" y2="14"/>
                </svg>
                Monitoreo General
            </h2>
        </div>
        <div class="monitoreo-toolbar">
            <div class="monitoreo-period-tabs">
                <button class="monitoreo-period-tab active" data-period="6h">6 horas</button>
                <button class="monitoreo-period-tab" data-period="24h">24 horas</button>
            </div>
            <button class="btn-sm" id="btn-monitoreo-refresh">Actualizar</button>
        </div>
        <div class="monitoreo-chart-wrap">
            <canvas id="monitoreo-chart"></canvas>
            <div class="monitoreo-tooltip" id="monitoreo-tooltip"></div>
        </div>
        <div class="monitoreo-legend" id="monitoreo-legend"></div>
    </div>`;
  }

  // ── Init: Valvula ─────────────────────────────────────────────────
  function initValvula() {
    const valveDevs = state.devices.filter(d => d.attributes?.sensor_type === 'valve');
    const devSelect = document.getElementById('valve-device-select');
    const devName = document.getElementById('valve-device-name');
    const statusDot = document.querySelector('.valvula-state-dot');
    const statusText = document.getElementById('valve-status-text');
    const btnOn = document.getElementById('btn-valve-on');
    const btnOff = document.getElementById('btn-valve-off');
    const feedback = document.getElementById('valve-feedback');

    let selectedId = null;
    let loopInterval = null;

    function updateUI() {
      if (!selectedId) return;
      const dev = state.devices.find(d => d.id === selectedId);
      if (!dev) return;
      const tel = dev.telemetry || {};
      const valveState = tel.valve !== undefined ? parseInt(tel.valve) : -1;

      if (valveState === 1) {
        statusDot.style.background = 'var(--success, #22c55e)';
        statusText.textContent = 'Abierta (ON)';
        btnOn.disabled = true;
        btnOff.disabled = false;
      } else if (valveState === 0) {
        statusDot.style.background = 'var(--danger, #ef4444)';
        statusText.textContent = 'Cerrada (OFF)';
        btnOn.disabled = false;
        btnOff.disabled = true;
      } else {
        statusDot.style.background = 'var(--warning, #f59e0b)';
        statusText.textContent = 'Sin datos...';
        btnOn.disabled = true;
        btnOff.disabled = true;
      }
    }

    async function sendCommand(stateVal) {
      if (!selectedId) return;
      btnOn.disabled = true;
      btnOff.disabled = true;
      feedback.textContent = 'Enviando comando...';
      feedback.style.color = 'var(--accent)';

      // Optimistic UI update
      if (stateVal === 1) {
        statusDot.style.background = 'var(--success, #22c55e)';
        statusText.textContent = 'Abierta (ON)';
      } else {
        statusDot.style.background = 'var(--danger, #ef4444)';
        statusText.textContent = 'Cerrada (OFF)';
      }

      try {
        const res = await api('/api/rpc/valve', {
          method: 'POST',
          body: JSON.stringify({ device_id: selectedId, state: stateVal }),
        });
        const data = await res.json();
        if (data.ok) {
          feedback.textContent = `Comando ${stateVal ? 'ABRIR' : 'CERRAR'} enviado`;
          feedback.style.color = 'var(--success)';
        } else {
          feedback.textContent = 'Error al enviar comando';
          feedback.style.color = 'var(--danger)';
        }
      } catch (e) {
        feedback.textContent = 'Error de conexion';
        feedback.style.color = 'var(--danger)';
      }
      // Re-fetch devices and update UI
      setTimeout(async () => {
        await loadDevices();
        refreshDevices();
        updateUI();
      }, 1500);
    }

    btnOn.addEventListener('click', () => sendCommand(1));
    btnOff.addEventListener('click', () => sendCommand(0));

    function refreshDevices() {
      const current = selectedId;
      devSelect.innerHTML = '<option value="">Seleccionar nodo...</option>';
      const valveDevsNow = state.devices.filter(d => d.attributes?.sensor_type === 'valve');
      for (const d of valveDevsNow) {
        const opt = document.createElement('option');
        opt.value = d.id;
        opt.textContent = `${d.name} (${d.attributes?.zone || '?'})`;
        if (d.id === current) opt.selected = true;
        devSelect.appendChild(opt);
      }
      if (!valveDevsNow.length) {
        devName.textContent = 'No hay nodos actuadores. Crea uno desde Agregar Nodo.';
        btnOn.disabled = true;
        btnOff.disabled = true;
      }
    }

    devSelect.addEventListener('change', () => {
      selectedId = devSelect.value || null;
      if (selectedId) {
        const d = state.devices.find(d => d.id === selectedId);
        devName.textContent = d ? `${d.name} (${d.attributes?.zone || '?'})` : '';
        updateUI();
      }
    });

    refreshDevices();
    if (valveDevs.length > 0 && !selectedId) {
      devSelect.value = valveDevs[0].id;
      devSelect.dispatchEvent(new Event('change'));
    }

    loopInterval = setInterval(async () => {
      await loadDevices();
      refreshDevices();
      updateUI();
    }, 2000);

    const origCleanup = window._valveCleanup;
    if (origCleanup) origCleanup();
    window._valveCleanup = () => clearInterval(loopInterval);
  }

  // ── Markup: Agregar Nodo ────────────────────────────────────────
  function addNodeMarkup() {
    const defaultEdge = state.edges.length === 1
      ? `<input type="hidden" id="an-edge-id" value="${esc(state.edges[0].id)}" />
         <p class="an-edge-name">Edge: <strong>${esc(state.edges[0].name)}</strong></p>`
      : `<div class="form-group">
           <label>Edge</label>
           <select id="an-edge-id" class="customizable cs-sm">${state.edges.map(e =>
             `<option value="${esc(e.id)}">${esc(e.name)}</option>`
           ).join('')}</select>
         </div>`;

    return `<div class="add-node-container">
        <div class="page-header">
            <h2>
                <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="16"/><line x1="8" y1="12" x2="16" y2="12"/>
                </svg>
                Agregar Nodo
            </h2>
        </div>
        <div class="add-node-form">
            <div class="form-group">
                <label>Nombre del nodo</label>
                <input type="text" id="an-name" placeholder="Ej: Invernadero-1" maxlength="40" />
            </div>
            <div class="form-group">
                <label>Zona</label>
                <input type="text" id="an-zone" placeholder="Ej: Zona-A" maxlength="20" />
            </div>
            <div class="form-group">
                <label>Tipo de sensor</label>
                <select id="an-sensor" class="customizable cs-sm">
                    <option value="temperature">Temperatura</option>
                    <option value="humidity">Humedad</option>
                    <option value="light">Luminosidad</option>
                    <option value="valve">Actuador (Bomba)</option>
                </select>
            </div>
            ${defaultEdge}
            <div class="form-group">
                <label>Ubicacion en el mapa <span class="form-hint">(click para posicionar)</span></label>
                <div id="an-minimap" class="an-minimap"></div>
                <div class="an-coords">
                    <input type="text" id="an-lat" placeholder="Latitud" readonly />
                    <input type="text" id="an-lng" placeholder="Longitud" readonly />
                </div>
            </div>
            <button class="btn-primary" id="btn-create-node">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="16"/><line x1="8" y1="12" x2="16" y2="12"/></svg>
                Crear Nodo
            </button>
        </div>
        <div id="an-result" class="an-result" style="display:none"></div>
    </div>`;
  }

  // ── Init: Agregar Nodo ───────────────────────────────────────────
  function initAddNode() {
    initCustomSelects();
    const defaultLat = 5.0298;
    const defaultLng = -75.4715;
    const latEl = document.getElementById('an-lat');
    const lngEl = document.getElementById('an-lng');
    let anMarker = null;

    const minimap = L.map('an-minimap', {
      center: [defaultLat, defaultLng],
      zoom: 15,
      zoomControl: true,
      attributionControl: false,
      scrollWheelZoom: true,
    });
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
      maxNativeZoom: 19, maxZoom: 22,
      subdomains: ['a', 'b', 'c'],
    }).addTo(minimap);

    minimap.on('click', e => {
      if (anMarker) minimap.removeLayer(anMarker);
      anMarker = L.marker([e.latlng.lat, e.latlng.lng], {
        draggable: true,
        icon: L.divIcon({
          className: '',
          html: '<div style="width:22px;height:22px;background:#22c55e;border:3px solid white;border-radius:50%;box-shadow:0 4px 12px rgba(0,0,0,0.4);animation:pulse-dot 2s ease-in-out infinite;"></div>',
          iconSize: [22, 22],
          iconAnchor: [11, 11],
        }),
      }).addTo(minimap);
      anMarker.on('drag', () => {
        const ll = anMarker.getLatLng();
        latEl.value = ll.lat.toFixed(6);
        lngEl.value = ll.lng.toFixed(6);
      });
      latEl.value = e.latlng.lat.toFixed(6);
      lngEl.value = e.latlng.lng.toFixed(6);
    });

    setTimeout(() => minimap.invalidateSize(), 200);

    document.getElementById('btn-create-node').addEventListener('click', async () => {
      const name = document.getElementById('an-name').value.trim();
      const zone = document.getElementById('an-zone').value.trim();
      const sensorType = document.getElementById('an-sensor').value;
      const lat = parseFloat(latEl.value);
      const lng = parseFloat(lngEl.value);
      let edgeId = '';

      if (state.edges.length === 1) {
        edgeId = state.edges[0].id;
      } else {
        edgeId = document.getElementById('an-edge-id')?.value || '';
      }

      if (!name) { toast('Ingresa un nombre para el nodo', 'warning'); return; }
      if (!zone) { toast('Ingresa una zona', 'warning'); return; }
      if (isNaN(lat) || isNaN(lng)) { toast('Click en el mapa para posicionar el nodo', 'warning'); return; }

      const btn = document.getElementById('btn-create-node');
      btn.disabled = true;
      btn.textContent = 'Creando...';

      const res = await api('/api/devices/create', {
        method: 'POST',
        body: JSON.stringify({ name, zone, sensor_type: sensorType, lat, lng, edge_id: edgeId }),
      });

      btn.disabled = false;
      btn.innerHTML = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="16"/><line x1="8" y1="12" x2="16" y2="12"/></svg> Crear Nodo';

      if (!res || !res.ok) {
        toast('Error al crear el nodo', 'error');
        return;
      }

      const data = await res.json();
      const sensorLabels = { temperature: 'Temperatura', humidity: 'Humedad', light: 'Luminosidad', valve: 'Valvula' };
      const sv = SENSOR_VARS[data.sensor_type];

      const resultEl = document.getElementById('an-result');
      resultEl.style.display = 'block';
      resultEl.innerHTML = `
        <div class="an-success-card">
          <div class="an-success-icon">\u2713</div>
          <h3>Nodo creado exitosamente</h3>
          <div class="an-details">
            <div class="an-detail-row"><span>Nombre:</span> <strong>${esc(data.device_name)}</strong></div>
            <div class="an-detail-row"><span>Zona:</span> <strong>${esc(data.zone)}</strong></div>
            <div class="an-detail-row"><span>Sensor:</span> <strong>${sv.icon} ${sensorLabels[data.sensor_type]}</strong></div>
            <div class="an-detail-row"><span>Posicion:</span> <strong>${data.lat.toFixed(4)}, ${data.lng.toFixed(4)}</strong></div>
          </div>
          <div class="an-prov-key">
            <div class="an-prov-label">Access Token</div>
            <div class="an-prov-value" id="an-prov-value">${esc(data.access_token)}</div>
            <button class="btn-sm outline" id="btn-copy-key" onclick="navigator.clipboard.writeText('${esc(data.access_token)}').then(()=>App.toast('Token copiado al portapapeles','success'))" style="margin-top:8px">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>
              Copiar Token
            </button>
          </div>
          <div class="an-instructions">
            <p>Pega este token en el firmware del ESP32:</p>
            <code>#define PROV_KEY "${esc(data.access_token)}"</code>
          </div>
        </div>`;

      toast('Nodo creado: ' + data.device_name, 'success');
    });
  }
  async function initHistoricosPage() {
    if (state.devices.length === 0) {
      const res = await api('/api/devices');
      if (res) {
        const data = await res.json();
        state.devices = data.devices || [];
      }
    }
    const histSel = document.getElementById('hist-device');
    if (histSel) {
      const wrapper = histSel.closest('.custom-select');
      if (wrapper) {
        wrapper.parentNode.insertBefore(histSel, wrapper);
        wrapper.remove();
      }
      histSel.innerHTML = state.devices.map(d =>
        `<option value="${esc(d.id)}">${esc(d.name)}</option>`
      ).join('');
      initCustomSelects(histSel.parentElement || document);
    }
    if (typeof initHistoricos === 'function') initHistoricos();
  }

  // ── Custom Select ─────────────────────────────────────────────────
  function initCustomSelects(scope = document) {
    scope.querySelectorAll('select.customizable').forEach(sel => {
      if (sel.closest('.custom-select')) return;
      const wrapper = document.createElement('div');
      wrapper.className = 'custom-select' + (sel.classList.contains('cs-sm') ? ' cs-sm' : '');
      sel.parentNode.insertBefore(wrapper, sel);

      const trigger = document.createElement('div');
      trigger.className = 'cs-trigger';
      trigger.tabIndex = 0;
      const label = document.createElement('span');
      label.className = 'cs-label';
      const icon = document.createElement('span');
      icon.className = 'cs-icon';
      icon.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><polyline points="6 9 12 15 18 9"/></svg>';
      trigger.appendChild(label);
      trigger.appendChild(icon);

      const dropdown = document.createElement('div');
      dropdown.className = 'cs-dropdown';
      dropdown.style.display = 'none';

      const renderOptions = () => {
        label.textContent = sel.selectedOptions[0]?.textContent || sel.options[0]?.textContent || '';
        dropdown.innerHTML = '';
        Array.from(sel.options).forEach((opt, i) => {
          if (opt.value === '') return;
          const div = document.createElement('div');
          div.className = 'cs-option' + (opt.selected ? ' selected' : '');
          div.innerHTML = `<span class="cs-dot"></span>${opt.textContent}`;
          div.addEventListener('click', () => {
            sel.value = opt.value;
            sel.dispatchEvent(new Event('change', { bubbles: true }));
            renderOptions();
            close();
          });
          dropdown.appendChild(div);
        });
      };

      const open = () => {
        document.querySelectorAll('.custom-select.open').forEach(s => {
          if (s !== wrapper) s.classList.remove('open');
        });
        wrapper.classList.add('open');
        dropdown.style.display = '';
        renderOptions();
      };
      const close = () => { wrapper.classList.remove('open'); setTimeout(() => { dropdown.style.display = 'none'; }, 200); };

      trigger.addEventListener('click', () => wrapper.classList.contains('open') ? close() : open());
      trigger.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); open(); } });

      sel.addEventListener('change', () => renderOptions());

      document.addEventListener('click', e => {
        if (!wrapper.contains(e.target)) close();
      });

      wrapper.appendChild(sel);
      wrapper.appendChild(trigger);
      wrapper.appendChild(dropdown);
      renderOptions();
    });
  }

  // ── Setup inicial ────────────────────────────────────────────────
  function setup() {
    const stored = sessionStorage.getItem('username');
    const userNameEl = document.getElementById('user-name');
    const userAvatarEl = document.getElementById('user-avatar');
    if (userNameEl && stored) userNameEl.textContent = stored;
    if (userAvatarEl && stored) userAvatarEl.textContent = stored.charAt(0).toUpperCase();

    loadMe();

    document.querySelectorAll('.nav-link').forEach(link => {
      link.addEventListener('click', e => {
        e.preventDefault();
        navigate(link.dataset.page);
        closeDrawer();
      });
    });

    document.querySelectorAll('.bottom-nav-btn').forEach(btn => {
      btn.addEventListener('click', e => {
        e.preventDefault();
        const page = btn.dataset.page;
        navigate(page);
        document.querySelectorAll('.bottom-nav-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
      });
    });

    document.getElementById('btn-logout')?.addEventListener('click', async () => {
      await api('/api/logout', { method: 'POST' });
      window.location.href = '/login';
    });

    // Theme toggle (desktop + mobile)
    function toggleTheme() {
      const current = document.documentElement.getAttribute('data-theme');
      const next = current === 'dark' ? 'light' : 'dark';
      document.documentElement.setAttribute('data-theme', next);
      localStorage.setItem('theme', next);
    }
    const savedTheme = localStorage.getItem('theme') || 'dark';
    document.documentElement.setAttribute('data-theme', savedTheme);
    document.getElementById('btn-theme')?.addEventListener('click', toggleTheme);
    document.getElementById('btn-theme-mobile')?.addEventListener('click', toggleTheme);

    // Mobile drawer
    function openDrawer() {
      document.getElementById('sidebar')?.classList.add('drawer-open');
      document.getElementById('sidebar-overlay')?.classList.add('show');
    }
    function closeDrawer() {
      document.getElementById('sidebar')?.classList.remove('drawer-open');
      document.getElementById('sidebar-overlay')?.classList.remove('show');
    }
    document.getElementById('btn-mobile-drawer')?.addEventListener('click', openDrawer);
    document.getElementById('sidebar-overlay')?.addEventListener('click', closeDrawer);

    // Close drawer on escape
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape') closeDrawer();
    });

    if (window.location.pathname === '/dashboard' || window.location.pathname === '/') {
      document.getElementById('main-content').style.overflow = 'hidden';
    }

    loadEdges();
    initCustomSelects();
  }

  // ── Public API ───────────────────────────────────────────────────
  return {
    state,
    api,
    toast,
    esc,
    cssVar,
    formatUptime,
    getNodeSensorVar,
    setLiveVar,
    SENSOR_VARS,
    isDeviceActive,
    markerColor,
    loadDevices,
    selectNode,
    renderNodeInfo,
    navigate,
    initDashboard,
    initHistoricosPage,
    setup,
    loadHistoricos: () => { if (typeof loadHistoricos === 'function') loadHistoricos(); },
    deleteNode,
  };
})();

// Alias globales
const esc = App.esc;
const cssVar = App.cssVar;
const formatUptime = App.formatUptime;
const isDeviceActive = App.isDeviceActive;
const markerColor = App.markerColor;
const loadDevices = () => App.loadDevices();
const selectNode = (id) => App.selectNode(id);
const updateNodeInfo = (dev) => App.renderNodeInfo(dev);

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => App.setup());
} else {
  App.setup();
}
