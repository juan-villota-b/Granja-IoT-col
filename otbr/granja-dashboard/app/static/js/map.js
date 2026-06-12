/* ═══════════════════════════════════════════════════════════════════
   Granja Dashboard · map.js
   Mapa Leaflet interactivo con marcadores de sensores
   ═══════════════════════════════════════════════════════════════════ */

let map = null;
let markers = {};
let gatewayGroup = null;
let nodeGroup = null;
let connectionLines = null;

const CENTER_LAT = 5.0298;
const CENTER_LNG = -75.4715;
const SCALE = 0.002;
const ZOOM_THRESHOLD = 15;
const INITIAL_ZOOM = 12;

let _nodeCoords = {};

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
  if (isNaN(posX)) posX = 50;
  if (isNaN(posY)) posY = 50;
  return toLatLng(posX, posY);
}

function initMap(devices) {
  const container = document.getElementById('dashboard-map');
  if (!container) return;

  if (map) { map.remove(); map = null; markers = {}; }
  _nodeCoords = {};

  // Guardar el estado previo en memoria antes de que las tiles lo pisen
  var savedView = null;
  try { savedView = localStorage.getItem('granja_map_state'); } catch(e) { /* localStorage blocked */ }

  const nodes = devices.filter(d => !App.isGateway(d));
  nodes.forEach(d => { _nodeCoords[d.id] = getDeviceLatLng(d); });

  updateStatusBar(devices);

  // Restaurar vista si habia estado guardado
  var center = [CENTER_LAT, CENTER_LNG];
  var zoom = INITIAL_ZOOM;
  if (savedView) {
    try {
      var sv = JSON.parse(savedView);
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

  gatewayGroup = L.layerGroup().addTo(map);
  nodeGroup = L.layerGroup().addTo(map);

  devices.forEach(dev => addDeviceMarker(dev));
  drawConnections();
  updateZoomVisibility();

  map.on('zoomend', updateZoomVisibility);

  // Guardar cambios del usuario (solo despues de que todo este estable)
  var saveTimer = null;
  map.on('moveend', function() {
    if (!map) return;
    if (saveTimer) clearTimeout(saveTimer);
    saveTimer = setTimeout(function() {
      try {
        var c = map.getCenter();
        localStorage.setItem('granja_map_state', JSON.stringify({
          lat: c.lat, lng: c.lng, zoom: map.getZoom()
        }));
    } catch(e) { /* bad saved state, use defaults */ }
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

  // Recuperar estado al volver a la pestaña
  document.removeEventListener('visibilitychange', _onVisibilityChange);
  window._onVisibilityChange = async () => {
    if (document.visibilityState === 'visible') {
      await refreshDeviceTimestamps();
      refreshAllMarkers();
    }
  };
  document.addEventListener('visibilitychange', window._onVisibilityChange);
}

function updateStatusBar(devices) {
  const bar = document.getElementById('status-bar');
  if (!bar) return;
  const nodes = devices.filter(d => !App.isGateway(d));
  const total = nodes.length;
  const active = nodes.filter(d => App.isDeviceActive(d)).length;
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
  const attrs = dev.attributes || {};
  const tel = dev.telemetry || {};
  const gw = App.isGateway(dev);
  const active = App.isDeviceActive(dev);
  const devices = App.state.devices;

  let lat, lng;
  if (gw) {
    if (attrs.lat !== undefined && attrs.lng !== undefined) {
      lat = parseFloat(attrs.lat);
      lng = parseFloat(attrs.lng);
    } else {
      const ids = Object.keys(_nodeCoords);
      if (ids.length > 0) {
        const sum = ids.reduce((acc, id) => { acc[0] += _nodeCoords[id][0]; acc[1] += _nodeCoords[id][1]; return acc; }, [0, 0]);
        lat = sum[0] / ids.length;
        lng = sum[1] / ids.length;
      } else {
        lat = CENTER_LAT; lng = CENTER_LNG;
      }
    }
  } else {
    const c = _nodeCoords[dev.id] || getDeviceLatLng(dev);
    lat = c[0]; lng = c[1];
  }
  const zone = attrs.zone || attrs.Zone || '';

  const isValve = (dev.type || '').toLowerCase().includes('valve') || (dev.name || '').toLowerCase().includes('valve');
  const isSensor = (dev.type || '').toLowerCase().includes('sed') || (dev.type || '').toLowerCase().includes('th') || (dev.name || '').toLowerCase().includes('sensor') || (dev.name || '').toLowerCase().includes('th_auto');

  let markerColor, size, iconHtml;

  if (gw) {
    markerColor = '#8b5cf6';
    size = 36;
    iconHtml = `<div style="
      width:${size}px;height:${size}px;
      background:${markerColor};
      border:3px solid rgba(255,255,255,0.35);
      clip-path:polygon(50% 0%,100% 25%,100% 75%,50% 100%,0% 75%,0% 25%);
      box-shadow:0 0 20px rgba(139,92,246,0.4), 0 4px 16px rgba(0,0,0,0.5);
      display:flex;align-items:center;justify-content:center;
    "><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" style="position:relative;z-index:1">
      <path d="M5 12.55a11 11 0 0 1 14.08 0"/>
      <path d="M8.53 15.67a6 6 0 0 1 6.95 0"/>
      <circle cx="12" cy="20" r="1"/>
    </svg></div>`;
  } else {
    markerColor = isValve ? '#ef4444' : (isSensor ? '#22c55e' : '#3b82f6');
    size = 18;
    if (!active) markerColor = '#6b7280';
    const borderColor = active ? 'white' : '#9ca3af';
    const opacity = active ? '1' : '0.7';
    const pulse = active ? 'animation:pulse-dot 2s ease-in-out infinite;' : '';
    iconHtml = `<div style="width:${size}px;height:${size}px;background:${markerColor};border:2.5px solid ${borderColor};border-radius:50%;box-shadow:0 4px 10px rgba(0,0,0,0.35);opacity:${opacity};${pulse}"></div>`;
  }

  const icon = L.divIcon({ className: '', html: iconHtml, iconSize: [size, size], iconAnchor: [size / 2, size / 2] });

  const tooltipText = gw ? `📡 ${dev.name}` : `${dev.name}`;

  const marker = L.marker([lat, lng], { icon }).bindTooltip(tooltipText, {
    permanent: true,
    direction: 'top',
    offset: [0, -14],
    className: 'farm-tooltip',
  });

  let popupHtml;
  if (gw) {
    const nodeList = devices.filter(d => !App.isGateway(d));
    const activeNodes = nodeList.filter(d => App.isDeviceActive(d)).length;
    const inactiveNodes = nodeList.length - activeNodes;
    popupHtml = `<div style="min-width:200px">
      <div style="display:flex;align-items:center;gap:8px;margin-bottom:8px">
        <div style="width:28px;height:28px;background:#8b5cf6;clip-path:polygon(50% 0%,100% 25%,100% 75%,50% 100%,0% 75%,0% 25%);display:flex;align-items:center;justify-content:center">
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2.5"><circle cx="12" cy="20" r="1"/></svg>
        </div>
        <div>
          <strong style="font-size:1.05rem;color:#c4b5fd">${App.esc(dev.name)}</strong>
          <div style="font-size:0.7rem;color:var(--text-muted);margin-top:1px">Gateway Thread · Border Router</div>
        </div>
      </div>
      <hr style="border-color:var(--border);margin:10px 0" />
      <div style="display:flex;gap:16px;margin-bottom:6px">
        <div><span style="font-size:0.7rem;color:var(--text-dim)">Sensores</span><br><strong>${nodeList.length}</strong></div>
        <div><span style="font-size:0.7rem;color:var(--text-dim)">Activos</span><br><strong style="color:var(--success)">${activeNodes}</strong></div>
        <div><span style="font-size:0.7rem;color:var(--text-dim)">Inactivos</span><br><strong style="color:var(--danger)">${inactiveNodes}</strong></div>
      </div>
      <div style="margin-top:8px;font-size:0.78rem;color:var(--text-dim);font-style:italic">Centro de conexión de la red de sensores</div>
    </div>`;
  } else {
    const statusIcon = active ? '🟢' : '🔴';
    const statusText = active ? 'Activo' : 'Inactivo';
    const statusColor = active ? '#22c55e' : '#ef4444';
    const uptime = tel.uptime ? parseInt(tel.uptime) : 0;
    let uptimeStr = '--';
    if (uptime > 0) {
      const h = Math.floor(uptime / 3600);
      const m = Math.floor((uptime % 3600) / 60);
      uptimeStr = `${h}h ${m}m`;
    }
    let telemRows = '';
    const sensorVar = App.getNodeSensorVar(tel, dev.id);
    const sv = sensorVar ? App.SENSOR_VARS[sensorVar] : null;
    if (sv)
      telemRows += `<div class="popup-row"><span class="popup-label">${sv.icon} ${sv.label}</span><strong class="popup-value" style="color:${sv.color}">${App.esc(tel[sensorVar])} ${sv.unit}</strong></div>`;
    if (tel.rssi !== undefined && tel.rssi !== null)
      telemRows += `<div class="popup-row"><span class="popup-label">📡 RSSI</span><strong class="popup-value">${App.esc(tel.rssi)} dBm</strong></div>`;
    popupHtml = `<div class="node-popup">
      <div class="popup-header">
        <div class="popup-status-dot" style="background:${statusColor}"></div>
        <strong class="popup-title">${App.esc(dev.name)}</strong>
        <span class="popup-status" style="color:${statusColor}">${statusText}</span>
      </div>
      ${zone ? `<div class="popup-zone">📍 ${App.esc(zone)}</div>` : ''}
      <div class="popup-divider"></div>
      ${telemRows || '<div class="popup-row"><span class="popup-label" style="color:var(--text-dim)">Esperando datos...</span></div>'}
      <div class="popup-divider"></div>
      <div class="popup-footer">
        <span class="popup-label">⏱ Uptime</span>
        <strong class="popup-value">${uptimeStr}</strong>
      </div>
    </div>`;
  }
  marker.bindPopup(popupHtml);

  marker.on('click', () => {
    if (!gw) {
      App.selectNode(dev.id);
    }
  });

  marker.isGateway = gw;
  marker._devId = dev.id;
  marker._name = dev.name;
  marker._active = active;
  marker._isValve = isValve;
  marker._isSensor = isSensor;
  marker._zone = zone;

  if (gw) {
    marker.addTo(gatewayGroup);
  } else {
    marker.addTo(nodeGroup);
  }

  markers[dev.id] = marker;
}

function drawConnections() {
  if (connectionLines) map.removeLayer(connectionLines);
  connectionLines = L.layerGroup();

  const gateways = App.state.devices.filter(d => App.isGateway(d));
  if (gateways.length === 0) return;

  const gwDev = gateways[0];
  const gwMarker = markers[gwDev.id];
  if (!gwMarker) return;
  const gwLatLng = gwMarker.getLatLng();

  const nodes = App.state.devices.filter(d => !App.isGateway(d));
  nodes.forEach(node => {
    const m = markers[node.id];
    if (!m) return;
    const active = App.isDeviceActive(node);
    const line = L.polyline([gwLatLng, m.getLatLng()], {
      color: active ? 'rgba(139, 92, 246, 0.25)' : 'rgba(107, 114, 128, 0.12)',
      weight: 1.5,
      dashArray: '6 4',
      interactive: false,
    });
    connectionLines.addLayer(line);
  });

  connectionLines.addTo(map);
}

function updateZoomVisibility() {
  if (!map) return;
  const zoom = map.getZoom();
  const showDetail = zoom >= ZOOM_THRESHOLD;
  const zoomHint = document.getElementById('zoom-hint');
  if (zoomHint) zoomHint.style.opacity = showDetail ? '0' : '1';

  // Gateway siempre visible
  if (!map.hasLayer(gatewayGroup)) map.addLayer(gatewayGroup);

  // Nodos solo con zoom
  if (showDetail) {
    if (!map.hasLayer(nodeGroup)) map.addLayer(nodeGroup);
  } else {
    if (map.hasLayer(nodeGroup)) map.removeLayer(nodeGroup);
  }

  // Lineas de conexion solo con zoom
  if (connectionLines) {
    if (showDetail) {
      if (!map.hasLayer(connectionLines)) map.addLayer(connectionLines);
    } else {
      if (map.hasLayer(connectionLines)) map.removeLayer(connectionLines);
    }
  }
}

function updateMapHighlight(deviceId) {
  Object.entries(markers).forEach(([id, m]) => {
    if (id === deviceId && !m.isGateway) {
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
  if (marker.isGateway) return;
  const tel = telemetry || {};

  const dev = (App.state.devices || []).find(d => d.id === deviceId);
  const active = dev ? App.isDeviceActive(dev) : marker._active;

  if (active !== marker._active) {
    marker._active = active;
    const iconColor = !active ? '#6b7280'
      : marker._isValve ? '#ef4444'
      : marker._isSensor ? '#22c55e'
      : '#3b82f6';
    const opacity = active ? '1' : '0.7';
    const pulse = active ? 'animation:pulse-dot 2s ease-in-out infinite;' : '';
    const size = 18;
    marker.setIcon(L.divIcon({
      className: '',
      html: `<div style="width:${size}px;height:${size}px;background:${iconColor};border:2.5px solid ${active ? 'white' : '#9ca3af'};border-radius:50%;box-shadow:0 4px 10px rgba(0,0,0,0.35);opacity:${opacity};${pulse}"></div>`,
      iconSize: [size, size],
      iconAnchor: [size / 2, size / 2],
    }));
  }

  const uptimeStr = typeof formatUptime === 'function' ? formatUptime(tel.uptime) : (tel.uptime || '--');
  const statusIcon = active ? '🟢' : '🔴';
  const statusText = active ? 'Activo' : 'Inactivo';
  const statusColor = active ? '#22c55e' : '#ef4444';

  let telemRows = '';
  const sensorVar = App.getNodeSensorVar(tel, deviceId);
  const sv = sensorVar ? App.SENSOR_VARS[sensorVar] : null;
  if (sv)
    telemRows += `<div class="popup-row"><span class="popup-label">${sv.icon} ${sv.label}</span><strong class="popup-value" style="color:${sv.color}">${App.esc(tel[sensorVar])} ${sv.unit}</strong></div>`;
  if (tel.rssi !== undefined && tel.rssi !== null)
    telemRows += `<div class="popup-row"><span class="popup-label">📡 RSSI</span><strong class="popup-value">${App.esc(tel.rssi)} dBm</strong></div>`;

  const html = `<div class="node-popup">
    <div class="popup-header">
      <div class="popup-status-dot" style="background:${statusColor}"></div>
      <strong class="popup-title">${App.esc(marker._name)}</strong>
      <span class="popup-status" style="color:${statusColor}">${statusText}</span>
    </div>
    ${marker._zone ? `<div class="popup-zone">📍 ${App.esc(marker._zone)}</div>` : ''}
    <div class="popup-divider"></div>
    ${telemRows || '<div class="popup-row"><span class="popup-label" style="color:var(--text-dim)">Esperando datos...</span></div>'}
    <div class="popup-divider"></div>
    <div class="popup-footer">
      <span class="popup-label">⏱ Uptime</span>
      <strong class="popup-value">${uptimeStr}</strong>
    </div>
  </div>`;
  marker.setPopupContent(html);

  const tooltip = marker.getTooltip();
  if (tooltip) tooltip.setContent(`${App.esc(marker._name)}`);
  drawConnections();
}

function refreshAllMarkers() {
  if (!App || !App.state || !App.state.devices) return;
  App.state.devices.forEach(dev => {
    const marker = markers[dev.id];
    if (!marker || marker.isGateway) return;
    const active = App.isDeviceActive(dev);
    marker._active = active;
    const iconColor = !active ? '#6b7280'
      : marker._isValve ? '#ef4444'
      : marker._isSensor ? '#22c55e'
      : '#3b82f6';
    const opacity = active ? '1' : '0.7';
    const pulse = active ? 'animation:pulse-dot 2s ease-in-out infinite;' : '';
    const size = 18;
    marker.setIcon(L.divIcon({
      className: '',
      html: `<div style="width:${size}px;height:${size}px;background:${iconColor};border:2.5px solid ${active ? 'white' : '#9ca3af'};border-radius:50%;box-shadow:0 4px 10px rgba(0,0,0,0.35);opacity:${opacity};${pulse}"></div>`,
      iconSize: [size, size],
      iconAnchor: [size / 2, size / 2],
    }));
    refreshMarkerPopup(dev.id);
  });
  drawConnections();
  updateStatusBar(App.state.devices);
}

// Refrescar datos de todos los nodos cada 5s sin reinit el mapa
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

function refreshMarkerPopup(deviceId) {
  const marker = markers[deviceId];
  if (!marker || marker.isGateway) return;
  const dev = (App.state.devices || []).find(d => d.id === deviceId);
  if (!dev) return;
  const tel = dev.telemetry || {};
  const active = App.isDeviceActive(dev);
  const uptimeStr = typeof formatUptime === 'function' ? formatUptime(tel.uptime) : (tel.uptime || '--');
  const statusIcon = active ? '🟢' : '🔴';
  const statusText = active ? 'Activo' : 'Inactivo';
  const statusColor = active ? '#22c55e' : '#ef4444';
  const sensorVar = App.getNodeSensorVar(tel, deviceId);
  const sv = sensorVar ? App.SENSOR_VARS[sensorVar] : null;
  let telemRows = '';
  if (sv)
    telemRows += `<div class="popup-row"><span class="popup-label">${sv.icon} ${sv.label}</span><strong class="popup-value" style="color:${sv.color}">${App.esc(tel[sensorVar])} ${sv.unit}</strong></div>`;
  if (tel.rssi !== undefined && tel.rssi !== null)
    telemRows += `<div class="popup-row"><span class="popup-label">📡 RSSI</span><strong class="popup-value">${App.esc(tel.rssi)} dBm</strong></div>`;
  const html = `<div class="node-popup">
    <div class="popup-header">
      <div class="popup-status-dot" style="background:${statusColor}"></div>
      <strong class="popup-title">${App.esc(marker._name)}</strong>
      <span class="popup-status" style="color:${statusColor}">${statusText}</span>
    </div>
    <div class="popup-divider"></div>
    ${telemRows || '<div class="popup-row"><span class="popup-label" style="color:var(--text-dim)">Esperando datos...</span></div>'}
    <div class="popup-divider"></div>
    <div class="popup-footer">
      <span class="popup-label">⏱ Uptime</span>
      <strong class="popup-value">${uptimeStr}</strong>
    </div>
  </div>`;
  marker.setPopupContent(html);
}