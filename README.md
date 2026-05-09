# DataAcquisitionEsp32 Remote Control Endpoints

When the ESP32 SoftAP is running, these HTTP endpoints are available:

- `GET /` shows the dashboard.
- `GET /browser/` shows the file browser and upload form.
- `GET /api/status` returns a JSON status snapshot (`uptime_ms`, heap, station count, firmware version).
- `GET /api/monitor` streams the same ESP log lines you see on serial and stays open.
- `POST /api/reset` schedules a reboot.
- `POST /api/ota` accepts a firmware binary in the request body, switches OTA slot, and reboots.

## Example usage

```bash
# Dashboard
curl http://192.168.4.1/

# File browser
curl http://192.168.4.1/browser/

# Status snapshot
curl http://192.168.4.1/api/status

# Stream serial log output
curl -N http://192.168.4.1/api/monitor

# Remote reset
curl -X POST http://192.168.4.1/api/reset

# OTA update (firmware binary)
curl -X POST --data-binary @build/esp-daq.bin \
  http://192.168.4.1/api/ota
```
