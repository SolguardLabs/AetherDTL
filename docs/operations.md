# Operaciones

## Referencias De Distribución

Una entrega estable exige que `main`, `production` y el tag de versión resuelvan al mismo SHA. La
rama `production` no recibe commits directos.

```mermaid
flowchart LR
    F["Feature branch"] --> PR["Pull request"]
    PR --> CI["Quality + sanitizers"]
    CI --> M["main"]
    M --> P["production"]
    P --> T["tag v1.0.0"]
    T --> R["Release"]
```

## Build Reproducible

Linux:

```bash
npm ci
bash scripts/ci.sh
```

Windows:

```powershell
npm.cmd ci
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\ci.ps1
```

No se requiere cambiar la política global de PowerShell. El wrapper usa `npm.cmd` y el build carga
MSVC mediante `vswhere` si es necesario.

## Señales A Observar

| Campo                                   | Acción                                    |
| --------------------------------------- | ----------------------------------------- |
| `security.paused = true`                | detener enrutamiento y conservar snapshot |
| `security.reserve_coverage_bps < 12500` | reducir admisión y fondear vaults         |
| `operator_concentration > 7500`         | redistribuir flujo entre operadores       |
| `lane_concentration > 8500`             | habilitar inventario alternativo          |
| `economic_risk.shortfall > 0`           | detener crecimiento de exposición         |
| cualquier invariante `false`            | aislar el proceso y reconciliar           |

## Runbook De Pausa

1. registrar motivo y hora;
2. activar pausa con cuenta `guardian`;
3. capturar `state_digest`, balances, events, `economic_risk` y `security`;
4. ejecutar la conciliación independiente;
5. identificar el último plan confirmado;
6. aplicar la corrección en una rama aislada;
7. ejecutar CI y sanitizers;
8. reanudar y comprobar el evento `pause_changed`.

## Conciliación

Comprobar como mínimo:

- suma de balances por activo antes y después;
- source debitado a usuarios frente a source recibido por operadores;
- target debitado a vaults frente a créditos de owner, operador y fee collector;
- número de planes ejecutados frente a eventos `plan_executed`;
- lanes usadas frente al catálogo habilitado;
- floors de cada vault.

## Backups

La persistencia externa debe conservar snapshots JSON y journal por digest. Para reconstrucción,
procesa snapshots en orden de clock y rechaza cualquier salto cuyo digest anterior no coincida con
el checkpoint esperado.
