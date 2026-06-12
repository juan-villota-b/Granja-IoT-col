/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · app.js
   Core: estado, API, router, toasts, utilidades
   ═══════════════════════════════════════════════════════════════════ */

const App = (() => {

  // ── Estado centralizado ──────────────────────────────────────────
  const state = {
    devices: [],
    activeNodeId: null,
    currentPage: 'dashboard',
  };

  // Rastrea la variable real de cada nodo segun el ultimo dato vivo
  const _liveVar = {}; // deviceId → 'temperature'|'humidity'|'light'

  // ── Sanitización ─────────────────────────────────────────────────
  function esc(str) {
    if (str === null || str === undefined) return '';
    const s = String(str);
    const div = document.createElement('div');
    div.textContent = s;
    return div.innerHTML;
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
      toast('Error de conexión', 'error');
      return null;
    }
  }

  // ── Toast system ─────────────────────────────────────────────────
  function toast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    if (!container) return;
    const el = document.createElement('div');
    el.className = `toast ${type}`;
    const icons = { success: '✓', error: '✕', info: 'ℹ', warning: '⚠' };
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

  function batteryPercent(mV) {
    const mv = parseFloat(mV);
    if (isNaN(mv)) return null;
    // Asumimos rango 3000mV (vacía) - 4200mV (llena) para Li-Ion
    const pct = Math.max(0, Math.min(100, Math.round(((mv - 3000) / 1200) * 100)));
    return pct;
  }

  function batteryColor(pct) {
    if (pct === null) return 'var(--text-dim)';
    if (pct > 60) return 'var(--success)';
    if (pct > 30) return 'var(--accent)';
    return 'var(--danger)';
  }

  // ── Carga de dispositivos ────────────────────────────────────────
  async function loadDevices() {
    const res = await api('/api/devices');
    if (!res) return;
    const data = await res.json();
    state.devices = data.devices || [];

    const selector = document.getElementById('node-selector');
    if (selector) {
      // remover wrapper existente para reinicializar tras poblado
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

    // Actualizar badge de activos
    const badge = document.getElementById('badge-active');
    if (badge) {
      const nodeList = state.devices.filter(d => !isGateway(d));
      const active = nodeList.filter(d => isDeviceActive(d)).length;
      badge.textContent = active;
      badge.style.display = active > 0 ? '' : 'none';
    }

    if (typeof initMap === 'function') initMap(state.devices);
  }

  // ── Selección de nodo ────────────────────────────────────────────
  function selectNode(deviceId) {
    const dev = state.devices.find(d => d.id === deviceId);
    if (!dev) return;
    state.activeNodeId = deviceId;

    const tel = dev.telemetry || {};
    const sensorVar = getNodeSensorVar(tel, deviceId);
    const sv = sensorVar ? SENSOR_VARS[sensorVar] : null;

    const chartsContainer = document.getElementById('panel-charts');
    if (chartsContainer) {
      chartsContainer.innerHTML = `
        <div class="chart-container chart-primary">
          <h4>${sv ? `<span style="color:${sv.color}">${sv.icon}</span> ${sv.label}` : '🌱 Sensor'}</h4>
          <canvas id="chart-sensor"></canvas>
        </div>
        <div class="chart-container chart-rssi">
          <h4>📡 RSSI</h4>
          <canvas id="chart-rssi"></canvas>
        </div>`;
    }

    renderNodeInfo(dev);
    if (typeof startRealtimeCharts === 'function') startRealtimeCharts(deviceId);
    if (typeof updateMapHighlight === 'function') updateMapHighlight(deviceId);
    document.getElementById('node-selector').value = deviceId;
  }

  function renderNodeInfo(dev) {
    const info = document.getElementById('node-info');
    if (!info) return;
    const tel = dev.telemetry || {};
    const attrs = dev.attributes || {};
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
      <div class="info-label">📡 RSSI</div>
      <div class="info-value">${esc(tel.rssi)} <small>dBm</small></div>
    </div>`;

    info.innerHTML = `
      <div class="node-info-header">
        <div class="node-indicator${active ? '' : ' inactive'}" style="background:${active ? 'var(--success)' : 'var(--danger)'}"></div>
        <span class="node-info-title">${esc(dev.name)}</span>
        <span class="node-info-type">${esc(dev.type)}</span>
      </div>
      <div class="node-info-grid two-card">${cards}</div>`;
  }

  function getNodeSensorVar(tel, deviceId) {
    if (deviceId && _liveVar[deviceId]) return _liveVar[deviceId];
    if (tel.temperature !== undefined && tel.temperature !== null) return 'temperature';
    if (tel.humidity !== undefined && tel.humidity !== null) return 'humidity';
    if (tel.light !== undefined && tel.light !== null) return 'light';
    return null;
  }

  function setLiveVar(deviceId, sensorKey) {
    if (sensorKey) _liveVar[deviceId] = sensorKey;
  }

  const SENSOR_VARS = {
    temperature: { label: 'Temperatura', unit: '°C', color: '#f59e0b', icon: '🌡️' },
    humidity:    { label: 'Humedad', unit: '%', color: '#06b6d4', icon: '💧' },
    light:       { label: 'Luminosidad', unit: 'lux', color: '#fbbf24', icon: '☀️' },
  };

  function isDeviceActive(dev) {
    const tel = dev.telemetry || {};
    const hasData = (tel.temperature !== undefined && tel.temperature !== null)
                 || (tel.humidity !== undefined && tel.humidity !== null)
                 || (tel.light !== undefined && tel.light !== null)
                 || (tel.battery !== undefined && tel.battery !== null);
    if (!hasData) return false;

    const attrs = dev.attributes || {};
    const now = Date.now();
    const TIMEOUT = 600000; // 10 min

    const lastActivity = parseInt(attrs.lastActivityTime);
    if (!isNaN(lastActivity) && lastActivity > 0) return (now - lastActivity) < TIMEOUT;

    const ts = parseInt(tel._ts);
    if (!isNaN(ts) && ts > 0) return (now - ts) < TIMEOUT;

    return true; // tiene datos, asumir activo
  }

  function isGateway(dev) {
    return (dev.name || '').toLowerCase().includes('gateway');
  }

  // ── SPA Router ───────────────────────────────────────────────────
  function navigate(page) {
    state.currentPage = page;
    const main = document.getElementById('main-content');

    // Limpiar intervalos y websockets de otras páginas
    if (typeof ws !== 'undefined' && ws) { try { ws.close(); } catch(e) {} }
    if (typeof valveCheckInterval !== 'undefined' && valveCheckInterval) { clearInterval(valveCheckInterval); valveCheckInterval = null; }

    main.classList.add('fading');
    setTimeout(() => {
      switch (page) {
        case 'dashboard':
          main.style.overflow = 'hidden';
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
        case 'config':
          main.style.overflow = 'auto';
          main.innerHTML = configMarkup();
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
  }

  // ── Inicialización Dashboard ─────────────────────────────────────
  async function initDashboard() {
    document.getElementById('main-content').style.overflow = 'hidden';
    await loadDevices();
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
                        Selecciona un nodo en el mapa<br>o en el selector para ver su telemetría
                    </p>
                </div>
                <div class="panel-charts" id="panel-charts"></div>
            </div>
        </div>
    </div>`;
  }

  // ── Markup: Históricos ───────────────────────────────────────────
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
                Históricos
            </h2>
            <button class="btn-sm outline" id="btn-export-csv" title="Exportar a CSV">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                Exportar CSV
            </button>
        </div>
        <div class="hist-tabs">
            <button class="hist-tab active" data-view="24h">Últimas 24h</button>
            <button class="hist-tab" data-view="30d">Últimos 30 días</button>
            <button class="hist-tab" data-view="custom">Rango personalizado</button>
        </div>
        <div class="hist-controls">
            <select id="hist-device" class="customizable cs-sm"><option value="">Cargando...</option></select>
            <select id="hist-variable" class="customizable cs-sm">
                <option value="temperature">Temperatura</option>
                <option value="humidity">Humedad</option>
                <option value="light">Luminosidad</option>
                <option value="battery">Batería</option>
            </select>
            <div id="hist-custom-controls">
                <input type="date" id="hist-start" value="${startISO}" />
                <input type="date" id="hist-end" value="${endISO}" />
                <select id="hist-interval" class="customizable cs-sm">
                    <option value="3600000">1 hora</option>
                    <option value="21600000">6 horas</option>
                    <option value="43200000">12 horas</option>
                    <option value="86400000">24 horas</option>
                </select>
            </div>
            <button class="btn-sm" onclick="App.loadHistoricos()">Consultar</button>
        </div>
        <div id="hist-summary" class="hist-summary"></div>
        <div class="hist-chart-container"><canvas id="hist-chart"></canvas></div>
        <div class="hist-table-container">
            <table class="hist-table">
                <thead><tr><th>Fecha/Hora</th><th>Promedio</th><th>Mínimo</th><th>Máximo</th></tr></thead>
                <tbody id="hist-table-body"></tbody>
            </table>
            <div class="hist-pagination" id="hist-pagination"></div>
        </div>
    </div>`;
  }

  // ── Markup: Válvula ──────────────────────────────────────────────
  function valvulaMarkup() {
    return `<div class="valvula-container">
        <div class="page-header">
            <h2>
                <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M12 22c1-2 2-4 2-7 0-3-2-5-2-7"/><path d="M12 22c-1-2-2-4-2-7 0-3 2-5 2-7"/><path d="M6 15c3 3 9 3 12 0"/><path d="M4 7c0 4 3 7 8 10"/><path d="M20 7c0 4-3 7-8 10"/>
                </svg>
                Control de Válvula
            </h2>
        </div>
        <div class="valvula-status-card">
            <div class="valvula-status-indicator" id="valve-status-indicator">
                <div class="valvula-state-dot" style="background:var(--danger)"></div>
                <span id="valve-status-text">Desconectada</span>
            </div>
            <p class="valvula-device-name" id="valve-device-name">Esperando nodo válvula...</p>
        </div>
        <div class="valvula-btn-container">
            <button class="btn-valve btn-valve-open" id="btn-valve-on" disabled>
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><path d="M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z"/></svg>
                ABRIR VÁLVULA
            </button>
            <button class="btn-valve btn-valve-close" id="btn-valve-off" disabled>
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                CERRAR VÁLVULA
            </button>
        </div>
        <p class="valve-feedback" id="valve-feedback"></p>
    </div>`;
  }

  // ── Markup: Configuración ────────────────────────────────────────
  function configMarkup() {
    return `<div class="config-container">
        <div class="page-header">
            <h2>
                <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <circle cx="12" cy="12" r="3"/><path d="M12 1v2"/><path d="M12 21v2"/><path d="M4.22 4.22l1.42 1.42"/><path d="M18.36 18.36l1.42 1.42"/><path d="M1 12h2"/><path d="M21 12h2"/><path d="M4.22 19.78l1.42-1.42"/><path d="M18.36 5.64l1.42-1.42"/>
                </svg>
                Reglas Automáticas
            </h2>
        </div>
        <div class="config-card">
            <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
                <rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/>
            </svg>
            <h3>Próximamente</h3>
            <p>Configuración de reglas automáticas en ThingsBoard para activar la válvula según umbrales de temperatura y humedad del cultivo.</p>
            <div class="config-fields">
                <div class="form-group"><label>Temperatura máxima (°C)</label><input type="number" disabled value="35" /></div>
                <div class="form-group"><label>Humedad mínima (%)</label><input type="number" disabled value="30" /></div>
                <div class="form-group"><label>Retardo activación (s)</label><input type="number" disabled value="10" /></div>
                <button class="btn-primary" disabled style="margin-top:12px">Guardar configuración</button>
            </div>
        </div>
    </div>`;
  }

  // ── Inicialización de la página de históricos ────────────────────
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
      // reinicializar wrapper
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
      if (sel.closest('.custom-select')) return; // ya inicializado
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
    // Nombre de usuario
    const stored = sessionStorage.getItem('username');
    const userNameEl = document.getElementById('user-name');
    const userAvatarEl = document.getElementById('user-avatar');
    if (userNameEl && stored) userNameEl.textContent = stored;
    if (userAvatarEl && stored) userAvatarEl.textContent = stored.charAt(0).toUpperCase();

    // Navegación
    document.querySelectorAll('.nav-link').forEach(link => {
      link.addEventListener('click', e => {
        e.preventDefault();
        navigate(link.dataset.page);
      });
    });

    // Logout
    document.getElementById('btn-logout')?.addEventListener('click', async () => {
      await api('/api/logout', { method: 'POST' });
      window.location.href = '/login';
    });

    // Theme toggle
    const btnTheme = document.getElementById('btn-theme');
    const savedTheme = localStorage.getItem('theme') || 'dark';
    document.documentElement.setAttribute('data-theme', savedTheme);
    btnTheme?.addEventListener('click', () => {
      const current = document.documentElement.getAttribute('data-theme');
      const next = current === 'dark' ? 'light' : 'dark';
      document.documentElement.setAttribute('data-theme', next);
      localStorage.setItem('theme', next);
    });

    // Mobile menu
    document.getElementById('mobile-menu-btn')?.addEventListener('click', () => {
      document.getElementById('sidebar-nav')?.classList.toggle('open');
    });

    // Skeleton inicial en dashboard
    if (window.location.pathname === '/dashboard' || window.location.pathname === '/') {
      document.getElementById('main-content').style.overflow = 'hidden';
    }

    initCustomSelects();
  }

  // ── Public API ───────────────────────────────────────────────────
  return {
    state,
    api,
    toast,
    esc,
    formatUptime,
    batteryPercent,
    batteryColor,
    getNodeSensorVar,
    setLiveVar,
    SENSOR_VARS,
    isDeviceActive,
    isGateway,
    loadDevices,
    selectNode,
    renderNodeInfo,
    navigate,
    initDashboard,
    initHistoricosPage,
    setup,
    // Expuesto para onclick en markup
    loadHistoricos: () => { if (typeof loadHistoricos === 'function') loadHistoricos(); },
  };
})();

// Alias globales para compatibilidad con otros módulos y onclick
const esc = App.esc;
const formatUptime = App.formatUptime;
const isDeviceActive = App.isDeviceActive;
const isGateway = App.isGateway;
const loadDevices = () => App.loadDevices();
const selectNode = (id) => App.selectNode(id);
const updateNodeInfo = (dev) => App.renderNodeInfo(dev);

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => App.setup());
} else {
  App.setup();
}