# Agency Desk Status Cube

Firmware para **M5Stack AtomS3** (ESP32-S3FN8) que muestra el estado de [Agency](https://agency.nabla.net) en una pantalla de 128x128 píxeles.

## Características

- Polling cada ~10 segundos a `https://agency.nabla.net/health/`
- Tres estados visuales con iconos grandes:
  - **ONLINE** (verde, ✓): `ok=true` y `boot_id` sin cambios
  - **REINICIO** (amarillo, ↻): servidor reiniciado (nuevo `boot_id`) o recuperándose
  - **ERROR** (rojo, ✗): fallo de conexión, HTTP no-200, JSON inválido, u `ok=false`
- WiFi configurable desde el móvil (sin hardcodear credenciales)
- RSSI mostrado en esquina superior izquierda

## Hardware

- **M5Stack AtomS3**: ESP32-S3FN8, 8MB flash, sin PSRAM
- Pantalla: 128x128 GC9107 IPS (la pantalla es también el botón)
- Botón en GPIO 41

## Controles

| Acción | Resultado |
|--------|-----------|
| **Click corto** | Forzar polling inmediato |
| **Mantener 2s** | Olvidar WiFi y reiniciar en modo AP |

## Compilar y Flashear

### Requisitos

- [PlatformIO](https://platformio.org/) (CLI o extensión VS Code)
- Cable USB-C

### Compilar

```bash
pio run
```

### Flashear

1. **Conectar** el AtomS3 por USB-C
2. **Entrar en modo bootloader**: mantener pulsado el botón **RST** durante ~2 segundos hasta que el LED se ponga verde
3. **Flashear**:
   ```bash
   pio run -t upload
   ```

### Monitor serial

```bash
pio device monitor
```

## Primera configuración WiFi

1. Al encender por primera vez, el AtomS3 mostrará **"wifi"** en amarillo
2. El dispositivo crea un punto de acceso llamado **"Agency-Atom"**
3. Conéctate a esa red desde tu móvil
4. Se abrirá automáticamente un portal para configurar tu WiFi
5. Selecciona tu red e introduce la contraseña
6. El AtomS3 se reiniciará y conectará automáticamente

Para **reconfigurar WiFi**: mantén pulsada la pantalla durante 2 segundos.

## Significado de los iconos

| Icono | Color | Estado | Significado |
|-------|-------|--------|-------------|
| ✓ (círculo verde) | Verde | ONLINE | Agency funcionando correctamente |
| ↻ (flechas) | Amarillo | REINICIO | Servidor reiniciado, esperando estabilización (~30s) |
| ✗ (equis) | Rojo | ERROR | Problema de conexión o Agency caído |
| WiFi arcos | Amarillo | CONFIG | Modo AP, esperando configuración WiFi |

## Credenciales WiFi opcionales (compile-time)

Si prefieres hardcodear credenciales (ej: para desarrollo), copia `include/secrets.example.h` a `include/secrets.h`:

```cpp
#define WIFI_SSID "tu_ssid"
#define WIFI_PASSWORD "tu_password"
```

Estas credenciales se intentarán antes de entrar en modo AP.

## Notas técnicas

- **HTTPS**: Se usa `setInsecure()` para saltar verificación de certificados (ahorra ~150KB de flash del cert bundle). Aceptable para un display de estado que solo hace GET a un endpoint conocido.
- **Sin PSRAM**: El código está optimizado para usar poca RAM.
- **USB CDC**: Habilitado para monitor serial por USB nativo del ESP32-S3.

## Estructura del proyecto

```
├── platformio.ini          # Configuración PlatformIO
├── src/
│   └── main.cpp            # Firmware principal
├── include/
│   └── secrets.example.h   # Plantilla para credenciales WiFi
└── README.md
```

## Licencia

MIT
