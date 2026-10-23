# Changelog

Todos los cambios relevantes de AetherDTL se documentan en este archivo.

## [2.0.0] - 2026-10-23

### Added

- puerta de capital económico sobre el estado posterior a cada plan, con rollback transaccional;
- escenario de admisión con reserva limitada y regresión de conservación contable.

### Changed

- el ciclo de vida y el límite de source consolidan la exposición de un intent entre estrategias;
- documentación de liquidación, seguridad y operaciones alineada con el flujo de capital.

## [1.0.0] - 2026-08-14

### Added

- pausa de emergencia gobernada y eventos de transición;
- idempotencia de planes y rollback transaccional;
- preflight agregado de inventario por vault;
- modelo de reserva con shocks correlacionados;
- monitor de seguridad con señales de concentración y cobertura;
- contrato JSON ampliado y documentación operativa en español;
- CI para Linux y Windows, más ASan y UBSan.

### Changed

- distribución fijada en Node.js 24 y C++20;
- versión de paquete elevada a `1.0.0`;
- instalación de CI congelada mediante `package-lock.json`.
