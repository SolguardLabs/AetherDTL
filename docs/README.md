# Documentación Técnica

Esta documentación describe el diseño operativo de AetherDTL 1.0.0.

| Documento                                               | Contenido                               |
| ------------------------------------------------------- | --------------------------------------- |
| [Arquitectura](./architecture.md)                       | componentes, estado y dependencias      |
| [Intents y firmas](./intents-and-signatures.md)         | payload canónico, nonce y ciclo de vida |
| [Liquidación y liquidez](./settlement-and-liquidity.md) | matching, fees, preflight y atomicidad  |
| [Modelo económico](./economic-model.md)                 | shocks, correlación y reserva requerida |
| [Modelo de seguridad](./security-model.md)              | roles, invariantes, señales y pausa     |
| [Operaciones](./operations.md)                          | despliegue, observabilidad e incidentes |
| [Integración](./integration.md)                         | CLI, JSON, importes y códigos de salida |

Los ejemplos usan enteros con seis decimales para `aUSDC`, `aEUR` y `aGBP`. Un importe
`500000000` representa 500 unidades del activo.
