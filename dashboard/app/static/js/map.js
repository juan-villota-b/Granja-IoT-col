/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · map.js
   Mapa Leaflet interactivo con marcadores de sensores
   ═══════════════════════════════════════════════════════════════════ */

let map = null;
let markers = {};
let nodeGroup = null;

const CENTER_LAT = 5.0298;
const CENTER_LNG = -75.4715;
const SCALE = 0.002;
const ZOOM_THRESHOLD = 18;
const INITIAL_ZOOM = 17;
const GATEWAY_LAT = 5.029181;
const GATEWAY_LNG = -75.472722;
const GATEWAY_NAME = 'Gateway-Raspberry';
let gatewayMarker = null;
let connectionLines = [];

function drawConnections() {
  if (!map) return;
  connectionLines.forEach(l => map.removeLayer(l));
  connectionLines = [];
  Object.keys(markers).forEach(id => {
    const m = markers[id];
    if (!m._active) return;
    const line = L.polyline([m.getLatLng(), [GATEWAY_LAT, GATEWAY_LNG]], {
      color: '#06b6d4', weight: 1.5, opacity: 0.5, dashArray: '4 6',
    }).addTo(map);
    connectionLines.push(line);
  });
}

function toLatLng(posX, posY) {
  const lat = CENTER_LAT + (posY - 50) * SCALE;
  const lng = CENTER_LNG + (posX - 50) * SCALE;
  return [lat, lng];
}

function getDeviceLatLng(dev) {
  const attrs = dev.attributes || {};
  if (attrs.lat !== undefined && attrs.lng !== undefined) {
    return [parseFloat(attrs.lat), parseFloat(attrs.lng)];
  }
  let posX = parseFloat(attrs.pos_x);
  let posY = parseFloat(attrs.pos_y);
  if (isNaN(posX)) posX = 50 + Math.random() * 20;
  if (isNaN(posY)) posY = 50 + Math.random() * 20;
  return toLatLng(posX, posY);
}

// ── Icono de marcador ─────────────────────────────────────────────
function _makeDivIcon(dev, active) {
  const isValve = (dev.type || '').toLowerCase().includes('valve')
               || (dev.name || '').toLowerCase().includes('valve');
  const isSensor = (dev.type || '').toLowerCase().includes('sed')
                || (dev.type || '').toLowerCase().includes('th')
                || (dev.name || '').toLowerCase().includes('sensor')
                || (dev.name || '').toLowerCase().includes('th_auto');

  const color = App.markerColor(active, isValve, isSensor);
  const size = 18;
  const borderColor = active ? 'white' : '#9ca3af';
  const opacity = active ? '1' : '0.7';
  const pulse = active ? 'animation:pulse-dot 2s ease-in-out infinite;' : '';
  const html = `<div style="width:${size}px;height:${size}px;background:${color};border:2.5px solid ${borderColor};border-radius:50%;box-shadow:0 4px 10px rgba(0,0,0,0.35);opacity:${opacity};${pulse}"></div>`;
  return L.divIcon({ className: '', html, iconSize: [size, size], iconAnchor: [size / 2, size / 2] });
}

// ── Popup HTML ────────────────────────────────────────────────────
function _buildPopupContent(dev, markerMeta) {
  const tel = dev.telemetry || {};
  const name = markerMeta ? markerMeta._name : dev.name;
  const zone = markerMeta ? markerMeta._zone : (dev.attributes || {}).zone || (dev.attributes || {}).Zone || '';
  const active = App.isDeviceActive(dev);

  const statusColor = active ? '#22c55e' : '#ef4444';
  const statusText = active ? 'Activo' : 'Inactivo';

  const uptime = active ? (tel.uptime ? parseInt(tel.uptime) : 0) : 0;
  let uptimeStr = '--';
  if (active && uptime > 0) {
    const h = Math.floor(uptime / 3600);
    const m = Math.floor((uptime % 3600) / 60);
    uptimeStr = `${h}h ${m}m`;
  }

  const sensorVar = App.getNodeSensorVar(tel, dev.id);
  const sv = sensorVar ? App.SENSOR_VARS[sensorVar] : null;
  let telemRows = '';
  if (sv)
    telemRows += `<div class="popup-row"><span class="popup-label">${sv.icon} ${sv.label}</span><strong class="popup-value" style="color:${sv.color}">${App.esc(tel[sensorVar])} ${sv.unit}</strong></div>`;
  if (tel.rssi !== undefined && tel.rssi !== null)
    telemRows += `<div class="popup-row"><span class="popup-label">\uD83D\uDCE1 RSSI</span><strong class="popup-value">${App.esc(tel.rssi)} dBm</strong></div>`;

  return `<div class="node-popup">
    <div class="popup-header">
      <div class="popup-status-dot" style="background:${statusColor}"></div>
      <strong class="popup-title">${App.esc(name)}</strong>
      <span class="popup-status" style="color:${statusColor}">${statusText}</span>
    </div>
    ${zone ? `<div class="popup-zone">\uD83D\uDCCD ${App.esc(zone)}</div>` : ''}
    <div class="popup-divider"></div>
    ${telemRows || '<div class="popup-row"><span class="popup-label" style="color:var(--text-dim)">Esperando datos...</span></div>'}
    <div class="popup-divider"></div>
    <div class="popup-footer">
      <span class="popup-label">\u23F1 Uptime</span>
      <strong class="popup-value">${uptimeStr}</strong>
    </div>
  </div>`;
}

// ── Init / Reset ──────────────────────────────────────────────────
function resetMap() {
  if (map) { map.remove(); map = null; markers = {}; }
  if (nodeGroup) nodeGroup.clearLayers();
  const info = document.getElementById('node-info');
  if (info) {
    info.innerHTML = `<p class="placeholder-text">
      <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" style="opacity:0.3;display:block;margin:0 auto 12px">
        <rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>
      </svg>
      Selecciona un nodo en el mapa<br>o en el selector para ver su telemetria
    </p>`;
  }
  const charts = document.getElementById('panel-charts');
  if (charts) charts.innerHTML = '';
  const mini = document.getElementById('node-history-mini');
  if (mini) mini.innerHTML = '';
  const sel = document.getElementById('node-selector');
  if (sel) sel.innerHTML = '<option value="">Selecciona un nodo...</option>';
}

function initMap(devices) {
  const container = document.getElementById('dashboard-map');
  if (!container) return;

  if (map) { map.remove(); map = null; markers = {}; }

  let savedView = null;
  try { savedView = localStorage.getItem('granja_map_state'); } catch(e) { /* localStorage blocked */ }

  updateStatusBar(devices);

  let center = [CENTER_LAT, CENTER_LNG];
  let zoom = INITIAL_ZOOM;
  if (savedView) {
    try {
      const sv = JSON.parse(savedView);
      if (typeof sv.lat === 'number' && typeof sv.lng === 'number' && typeof sv.zoom === 'number') {
        center = [sv.lat, sv.lng];
        zoom = sv.zoom;
      }
    } catch(e) { /* bad saved state, use defaults */ }
  }

  map = L.map('dashboard-map', {
    center: center,
    zoom: zoom,
    zoomControl: true,
    attributionControl: false,
  });

  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxNativeZoom: 19,
    maxZoom: 22,
    subdomains: ['a', 'b', 'c'],
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OSM</a>',
  }).addTo(map);

  nodeGroup = L.layerGroup().addTo(map);

  const gwIcon = L.divIcon({
    className: '',
    html: `<div style="position:relative;width:28px;height:28px">
      <div style="position:absolute;inset:0;background:linear-gradient(135deg,#0ea5e9,#6366f1);border:2.5px solid white;border-radius:50%;box-shadow:0 0 14px rgba(14,165,233,0.4),0 2px 6px rgba(0,0,0,0.3);display:flex;align-items:center;justify-content:center">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
          <path d="M5 12.55a11 11 0 0 1 14.08 0"/>
          <path d="M1.42 9a16 16 0 0 1 21.16 0"/>
          <path d="M8.53 16.11a6 6 0 0 1 6.95 0"/>
          <circle cx="12" cy="20" r="2"/>
        </svg>
      </div>
      <div style="position:absolute;top:-1px;right:-1px;width:9px;height:9px;background:#22c55e;border:1.5px solid white;border-radius:50%"></div>
    </div>`,
    iconSize: [28, 28],
    iconAnchor: [14, 14],
  });
  gatewayMarker = L.marker([GATEWAY_LAT, GATEWAY_LNG], { icon: gwIcon, zIndexOffset: 1000 })
    .bindTooltip(GATEWAY_NAME, { direction: 'top', offset: [0, -14] })
    .addTo(map);

  devices.forEach(dev => addDeviceMarker(dev));
  drawConnections();
  updateZoomVisibility();

  map.on('zoomend', updateZoomVisibility);

  let saveTimer = null;
  map.on('moveend', function() {
    if (!map) return;
    if (saveTimer) clearTimeout(saveTimer);
    saveTimer = setTimeout(function() {
      try {
        const c = map.getCenter();
        localStorage.setItem('granja_map_state', JSON.stringify({
          lat: c.lat, lng: c.lng, zoom: map.getZoom()
        }));
      } catch(e) { /* save failed */ }
    }, 300);
  });

  setTimeout(function() {
    try { map.invalidateSize(); } catch(e){ /* map not ready */ }
  }, 400);

  if (window._mapRefreshInterval) clearInterval(window._mapRefreshInterval);
  window._mapRefreshInterval = setInterval(async () => {
    await refreshDeviceTimestamps();
    refreshAllMarkers();
  }, 5000);

  let _onVisibilityChange;
  document.removeEventListener('visibilitychange', _onVisibilityChange);
  _onVisibilityChange = async () => {
    if (document.visibilityState === 'visible') {
      await refreshDeviceTimestamps();
      refreshAllMarkers();
    }
  };
  document.addEventListener('visibilitychange', _onVisibilityChange);
}

function updateStatusBar(devices) {
  const bar = document.getElementById('status-bar');
  if (!bar) return;
  const total = devices.length;
  const active = devices.filter(d => App.isDeviceActive(d)).length;
  bar.innerHTML = `
    <span class="status-item">
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/></svg>
      Total: <strong>${total}</strong>
    </span>
    <span class="status-item status-active">
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#22c55e" stroke-width="2"><circle cx="12" cy="12" r="4"/></svg>
      Activos: <strong>${active}</strong>
    </span>
    <span class="status-item status-inactive">
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ef4444" stroke-width="2"><circle cx="12" cy="12" r="4"/></svg>
      Inactivos: <strong>${total - active}</strong>
    </span>`;
}

function addDeviceMarker(dev) {
  const active = App.isDeviceActive(dev);
  const latlng = getDeviceLatLng(dev);

  const icon = _makeDivIcon(dev, active);
  const tooltipText = dev.name;

  const marker = L.marker(latlng, { icon }).bindTooltip(tooltipText, {
    permanent: true,
    direction: 'top',
    offset: [0, -14],
    className: 'farm-tooltip',
  });

  marker.bindPopup(_buildPopupContent(dev, null));
  marker.on('click', () => {
    App.selectNode(dev.id);
  });

  const isValve = (dev.type || '').toLowerCase().includes('valve')
               || (dev.name || '').toLowerCase().includes('valve');
  const isSensor = (dev.type || '').toLowerCase().includes('sed')
                || (dev.type || '').toLowerCase().includes('th')
                || (dev.name || '').toLowerCase().includes('sensor')
                || (dev.name || '').toLowerCase().includes('th_auto');
  const attrs = dev.attributes || {};
  const zone = attrs.zone || attrs.Zone || '';

  marker._devId = dev.id;
  marker._name = dev.name;
  marker._active = active;
  marker._isValve = isValve;
  marker._isSensor = isSensor;
  marker._zone = zone;

  marker.addTo(nodeGroup);
  markers[dev.id] = marker;
}

function updateZoomVisibility() {
  if (!map) return;
  const zoom = map.getZoom();
  const showDetail = zoom >= ZOOM_THRESHOLD;
  const zoomHint = document.getElementById('zoom-hint');
  if (zoomHint) zoomHint.style.opacity = showDetail ? '0' : '1';

  if (showDetail) {
    if (!map.hasLayer(nodeGroup)) map.addLayer(nodeGroup);
    drawConnections();
  } else {
    if (map.hasLayer(nodeGroup)) map.removeLayer(nodeGroup);
    connectionLines.forEach(l => map.removeLayer(l));
    connectionLines = [];
  }
}

function updateMapHighlight(deviceId) {
  Object.entries(markers).forEach(([id, m]) => {
    if (id === deviceId) {
      m.setZIndexOffset(1000);
      m.openPopup();
    } else {
      m.setZIndexOffset(0);
    }
  });
}

function updateMarkerTelemetry(deviceId, telemetry) {
  const marker = markers[deviceId];
  if (!marker) return;

  const dev = (App.state.devices || []).find(d => d.id === deviceId);
  const active = dev ? App.isDeviceActive(dev) : marker._active;
  if (!dev) return;

  if (active !== marker._active) {
    marker._active = active;
    marker.setIcon(_makeDivIcon(dev, active));
  }

  marker.setPopupContent(_buildPopupContent(dev, marker));
  const tooltip = marker.getTooltip();
  if (tooltip) tooltip.setContent(`${App.esc(marker._name)}`);
}

function refreshAllMarkers() {
  if (!App || !App.state || !App.state.devices) return;
  App.state.devices.forEach(dev => {
    const marker = markers[dev.id];
    if (!marker) return;
    const active = App.isDeviceActive(dev);
    marker._active = active;
    marker.setIcon(_makeDivIcon(dev, active));
    marker.setPopupContent(_buildPopupContent(dev, marker));
  });
  updateStatusBar(App.state.devices);
  if (typeof drawConnections === 'function') drawConnections();
}

async function refreshDeviceTimestamps() {
  try {
    const res = await fetch('/api/devices', { credentials: 'include' });
    if (res.status === 401) { window.location.reload(); return; }
    if (!res.ok) { console.warn('[ts] HTTP', res.status); return; }
    const data = await res.json();
    const fresh = data.devices || [];
    if (!App.state.devices) return;
    fresh.forEach(fd => {
      const existing = App.state.devices.find(d => d.id === fd.id);
      if (!existing) return;
      if (fd.telemetry) {
        if (!existing.telemetry) existing.telemetry = {};
        ['temperature','humidity','light','rssi','uptime','battery','_ts'].forEach(k => {
          if (fd.telemetry[k] !== undefined) existing.telemetry[k] = fd.telemetry[k];
        });
      }
      if (fd.attributes) {
        if (!existing.attributes) existing.attributes = {};
        ['lastActivityTime','zone','lat','lng'].forEach(k => {
          if (fd.attributes[k] !== undefined) existing.attributes[k] = fd.attributes[k];
        });
      }
    });
  } catch(e) { console.warn('[ts]', e.message || e); }
}
