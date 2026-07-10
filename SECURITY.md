# Security Policy

## Modelo De Seguridad

AetherDTL modela una red de intents donde el usuario firma los parametros
economicos principales y los operadores solo pueden ejecutar planes que pasan
validaciones locales de lane, precio, expiracion, autorizacion y saldo.

El motor asume:

- claves de firma registradas por cuenta;
- operadores autorizados explicitamente por el ledger;
- assets y lanes preconfigurados;
- balances enteros sin decimales flotantes;
- salida JSON determinista para auditoria y reproduccion.

## Invariantes Esperadas

- Ninguna cuenta puede quedar con balance negativo.
- Un intent ejecutable debe estar abierto, activo temporalmente y tener firma
  valida.
- Un plan debe referenciar el digest del intent aceptado.
- Cada slice debe usar una lane registrada y habilitada.
- Los fees se denominan en el asset destino de la lane.
- Las cancelaciones y expiraciones sacan al intent del conjunto ejecutable.

## Validacion Automatizada

La suite TypeScript valida los escenarios publicos mediante la CLI:

```bash
npm test
```

La validacion completa de CI usa:

```bash
npm run ci
```

## Dependencias

El core C++ no usa dependencias externas. Node.js se usa para build scripts y
tests. Dependabot esta configurado para npm y GitHub Actions.

## Alcance De Revision

El alcance principal es:

- `src/ledger.cpp`
- `src/intent.cpp`
- `src/matcher.cpp`
- `src/engine.cpp`
- `src/audit.cpp`
- `src/policy.cpp`
- `tests/node/*.test.ts`

## Reportes

Los reportes internos deben incluir:

- escenario afectado;
- estado JSON emitido por la CLI;
- secuencia de intents y planes;
- impacto contable o economico;
- propuesta de test de regresion.
