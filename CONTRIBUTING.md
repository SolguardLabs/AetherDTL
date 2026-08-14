# Contribuir A AetherDTL

## Flujo

1. crea una rama corta desde `main`;
2. limita cada cambio a una responsabilidad;
3. añade tests del comportamiento observable;
4. actualiza documentación y changelog cuando cambie el contrato;
5. ejecuta la puerta completa antes de abrir el pull request.

```bash
npm ci
npm run format:check
npm run ci
```

## Convenciones

- Código, nombres y comentarios en inglés.
- Documentación de producto en español.
- C++20 sin dependencias externas para el core.
- Importes mediante `Amount`; no uses coma flotante para movimientos contables.
- Funciones de reporting con referencias constantes.
- Toda mutación multi-etapa debe tener preflight o rollback.
- Todo nuevo campo JSON requiere un test de contrato.

## Pull Request

Incluye:

- propósito y límites del cambio;
- impacto en estado o contrato JSON;
- comandos ejecutados;
- resultado de tests, smoke y sanitizers;
- decisión de compatibilidad.

No incluyas secretos, binarios de build ni archivos de entorno.
