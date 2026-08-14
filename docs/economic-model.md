# Modelo Económico

## Objetivo

`EconomicRiskModel` estima reserva bajo un conjunto conservador de shocks. No modifica admisión ni
saldos; produce una vista para límites, reporting y respuesta operativa.

## Parámetros Por Defecto

| Parámetro                  |   bps | Interpretación                       |
| -------------------------- | ----: | ------------------------------------ |
| `source_price_shock_bps`   | 1.800 | caída del activo de origen           |
| `target_price_shock_bps`   | 1.250 | movimiento del activo de destino     |
| `liquidity_haircut_bps`    |   900 | coste de liquidación del destino     |
| `common_correlation_bps`   | 3.500 | factor común entre lanes             |
| `operator_default_bps`     | 1.500 | pérdida por incumplimiento operativo |
| `concentration_charge_bps` |   600 | carga sobre la mayor celda           |
| `operational_buffer_bps`   |   800 | reserva sobre notional total         |

## Normalización

Cada balance se convierte a la escala común de `kPriceScale = 1,000,000`:

```text
N(asset, amount) = amount × asset.price / kPriceScale
```

Una celda corresponde a una lane con planes ejecutados. Para la celda `i`:

```text
L_source,i   = N_source,i × source_shock
L_target,i   = N_target,i × target_shock
L_liquidity,i = N_target,i × liquidity_haircut
L_operator,i = N_source,i × operator_default

L_i = L_source,i + L_target,i + L_liquidity,i + L_operator,i
```

Todos los porcentajes se dividen por 10.000 y usan floor entero antes de agregarse.

## Correlación

La pérdida de factor común es:

```text
L_corr = floor(√((1 - ρ) × Σ L_i² + ρ × (Σ L_i)²))
```

Propiedades:

- con `ρ = 0`, las celdas se combinan por raíz de suma de cuadrados;
- con `ρ = 1`, la cola coincide con la suma de pérdidas;
- para una sola celda, `L_corr = L_1`;
- aumentar correlación no reduce la cola.

## Concentración Y Buffer

```text
C_largest = max(L_i) × concentration_charge_bps / 10,000
B_ops = (Σ N_source + Σ N_target) × operational_buffer_bps / 10,000
R_required = L_corr + C_largest + B_ops
```

La cobertura y el déficit son:

```text
coverage_bps = R_available × 10,000 / R_required
shortfall = max(R_required - R_available, 0)
```

Si no hay ejecuciones, `R_required = 0` y la cobertura se expresa como 10.000 bps.

## Ejemplo

Supón dos lanes con pérdidas estresadas de 120 y 80 unidades y correlación del 35 %:

```text
ΣL² = 120² + 80² = 20,800
(ΣL)² = 200² = 40,000
variance = 0.65 × 20,800 + 0.35 × 40,000 = 27,520
L_corr = floor(√27,520) = 165
```

La cola es superior a la mayor celda (`120`) e inferior a la suma sin diversificación (`200`).

## Interpretación Operativa

- `largest_cell_bps` muestra dependencia de una lane.
- `coverage_bps < 10,000` implica déficit inmediato bajo el escenario.
- Un incremento de `L_corr` sin crecimiento de notional suele indicar menor diversificación.
- `available_reserve` incluye balances de cuentas con rol `vault`, normalizados por precio.
