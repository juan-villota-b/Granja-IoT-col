"""
Controlador de riego automatico.

Evalua la ultima telemetria de todos los sensores para decidir
si abrir o cerrar la valvula de riego, usando umbrales configurables.
"""

import logging
import time
from datetime import datetime

log = logging.getLogger("irrigation")

DEFAULTS = {
    "enabled": True,
    "soil_low_pct": 30,
    "soil_high_pct": 70,
    "light_min_pct": 10,
    "temp_max_c": 35.0,
    "min_cycle_seconds": 60,
    "allowed_start_hour": 6,
    "allowed_end_hour": 20,
}


def _now_ms():
    return int(time.time() * 1000)


class IrrigationController:
    def __init__(self, actuator_nid, bridge, thresholds=None):
        self._actuator = actuator_nid
        self._bridge = bridge
        self._cfg = dict(DEFAULTS)
        if thresholds:
            self._cfg.update(thresholds)

        self._latest = {}
        self._last_cmd = None
        self._last_cmd_ts = 0

    @property
    def soil_low(self):
        return self._cfg["soil_low_pct"]

    @property
    def soil_high(self):
        return self._cfg["soil_high_pct"]

    @property
    def light_min(self):
        return self._cfg["light_min_pct"]

    @property
    def temp_max(self):
        return self._cfg["temp_max_c"]

    @property
    def min_cycle_s(self):
        return self._cfg["min_cycle_seconds"]

    def update_thresholds(self, overrides):
        self._cfg.update(overrides)
        log.info("Thresholds updated: %s", {k: self._cfg[k] for k in overrides})

    def feed(self, nid, telemetry):
        if not self._cfg["enabled"]:
            return

        self._latest[nid] = {
            "temperature": telemetry.get("temperature"),
            "humidity": telemetry.get("humidity"),
            "light": telemetry.get("light"),
            "valve": telemetry.get("valve"),
            "ts": _now_ms(),
        }

        if nid == self._actuator:
            valve_tel = telemetry.get("valve")
            if valve_tel is not None:
                self._last_cmd = int(valve_tel)
            return

        self._evaluate()

    def _evaluate(self):
        last = self._latest_snapshot()
        if not last:
            return

        humidity = last.get("humidity")
        temperature = last.get("temperature")
        light = last.get("light")

        if humidity is None:
            return

        now_s = time.time()

        valve_open = self._last_cmd == 1

        if valve_open:
            if humidity >= self.soil_high:
                self._command(0, f"Humedad alta ({humidity:.0f}% >= {self.soil_high}%)")
            return

        if humidity >= self.soil_low:
            return

        if light is not None and light < self.light_min:
            return

        if temperature is not None and temperature >= self.temp_max:
            return

        hour = datetime.now().hour
        if hour < self._cfg["allowed_start_hour"] or hour >= self._cfg["allowed_end_hour"]:
            return

        if self._last_cmd_ts and (now_s - self._last_cmd_ts) < self.min_cycle_s:
            remaining = int(self.min_cycle_s - (now_s - self._last_cmd_ts))
            log.info("AUTO: riego bloqueado — %ds restantes del ciclo minimo", remaining)
            return

        self._command(1, f"Humedad baja ({humidity:.0f}% < {self.soil_low}%)")

    def _latest_snapshot(self):
        temperature = None
        humidity = None
        light = None

        for nid, entry in self._latest.items():
            if nid == self._actuator:
                continue
            if entry.get("humidity") is not None:
                humidity = entry["humidity"]
            if entry.get("temperature") is not None:
                temperature = entry["temperature"]
            if entry.get("light") is not None:
                light = entry["light"]

        if humidity is not None:
            return {"humidity": humidity, "temperature": temperature, "light": light}
        return None

    def _command(self, state, reason):
        log.info("AUTO: %s %s", "ABRIR" if state else "CERRAR", reason)
        self._bridge.pending_commands[self._actuator] = {"v": state}
        self._last_cmd = state
        self._last_cmd_ts = time.time()
