---
title: "Cash Money"
date: 2026-03-27
tags: ["data", "economics"]
summary: "How many physical U.S. bills will be printed before January 2029, and how long will they stick around?"
---

For no particular reason, let's calculate how many physical American
dollar bills will be printed between today and January 20, 2029.

## The print orders

The Bureau of Engraving and Printing publishes annual print orders
for the Federal Reserve. Here's what we know:

- **CY 2025**: 4.1 to 5.9 billion notes
- **CY 2026**: 3.8 to 5.1 billion notes

Orders for 2027 and 2028 haven't been published yet. Using the 2026
range as a proxy is standard practice.

## The math

From today (March 27, 2026) through January 20, 2029:

| Period | Fraction of year | Estimated notes |
|---|---|---|
| Rest of 2026 (Mar 27 – Dec 31) | ~76% | 2.9B – 3.9B |
| All of 2027 | 100% | 3.8B – 5.1B |
| All of 2028 | 100% | 3.8B – 5.1B |
| Jan 1 – 20, 2029 | ~5.5% | 0.2B – 0.3B |
| **Total** | | **~10.7B – 14.4B** |

**Roughly 11 to 14 billion physical dollar bills.** Midpoint: about
12.5 billion notes.

Most of these aren't "new money" — they're replacements for worn-out
bills destroyed during normal processing. The Fed and BEP adjust
production throughout the year to match demand.

## How long do they stick around?

These new bills enter circulation over the printing period, then
slowly wear out and get pulled. The curve has two phases: a rise as
these new bills enter circulation through January 2029, then a long
gradual decline as they get destroyed.

The lifespan slider below matters more than you'd think. A $1 bill
lasts about 6 years. A $100 bill can survive 23 years. The default of
8 years is a rough weighted average across the denomination mix.

<div id="cash-chart-container" style="max-width: 720px; margin: 2em auto;">
  <div style="margin-bottom: 1em; font-family: inherit;">
    <label for="lifespan-slider" style="font-weight: 600;">Average bill lifespan:
      <span id="lifespan-value" style="font-variant-numeric: tabular-nums;">8</span> years
    </label>
    <br/>
    <input type="range" id="lifespan-slider" min="4" max="24" value="8" step="0.5"
           style="width: 100%; margin-top: 0.4em; accent-color: #e44;">
    <div style="display: flex; justify-content: space-between; font-size: 0.8em; opacity: 0.6;">
      <span>4 yr ($1 bills)</span>
      <span>24 yr ($100 bills)</span>
    </div>
  </div>
  <canvas id="cash-chart" width="720" height="400"
          style="width: 100%; border: 1px solid var(--border, #ccc); border-radius: 6px; background: var(--entry, #fff);">
  </canvas>
  <p style="font-size: 0.82em; opacity: 0.7; margin-top: 0.5em;">
    Percentage of all U.S. bills in circulation that were printed between Mar 2026 and Jan 2029.
    Assumes total circulation stays near 50 billion notes and uniform printing rate within each year.
  </p>
</div>

<script>
(function() {
  const canvas = document.getElementById('cash-chart');
  const ctx = canvas.getContext('2d');
  const slider = document.getElementById('lifespan-slider');
  const lifespanLabel = document.getElementById('lifespan-value');

  // High-DPI support
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  ctx.scale(dpr, dpr);
  const W = rect.width;
  const H = rect.height;

  // Constants
  const TOTAL_CIRCULATION = 50e9; // ~50 billion notes in circulation
  const ADMIN_NOTES_MID = 12.5e9; // midpoint estimate
  const PRINT_START = 2026.23; // Mar 27, 2026
  const PRINT_END = 2029.055; // Jan 20, 2029
  const PRINT_DURATION = PRINT_END - PRINT_START;
  const CHART_START = 2026;
  const CHART_END = 2051;
  const YEARS = CHART_END - CHART_START;

  function getComputedColor(prop, fallback) {
    const val = getComputedStyle(document.documentElement).getPropertyValue(prop).trim();
    return val || fallback;
  }

  function computeCurve(lifespan) {
    // Model: bills printed uniformly over [PRINT_START, PRINT_END].
    // Each bill survives for `lifespan` years (exponential decay, mean = lifespan).
    // At time t, fraction of admin bills still alive:
    //   For each printing moment s in [PRINT_START, PRINT_END]:
    //     survival(t, s) = exp(-(t - s) / lifespan)  if t > s, else 0
    //   Integrate over s and divide by PRINT_DURATION.

    const points = [];
    const steps = 500;
    const dt = YEARS / steps;

    for (let i = 0; i <= steps; i++) {
      const t = CHART_START + i * dt;
      let surviving = 0;

      if (t <= PRINT_START) {
        // Before any printing: use cumulative printed so far (none)
        // But account for partial year printing that already happened
        surviving = 0;
      } else {
        // Numerical integration over printing period
        const intSteps = 200;
        const sStart = PRINT_START;
        const sEnd = Math.min(t, PRINT_END);
        if (sEnd > sStart) {
          const ds = (sEnd - sStart) / intSteps;
          for (let j = 0; j <= intSteps; j++) {
            const s = sStart + j * ds;
            const age = t - s;
            const surv = Math.exp(-age / lifespan);
            surviving += surv * ds;
          }
          // surviving is in "years of printing" units; convert to fraction of total admin notes
          surviving = (surviving / PRINT_DURATION) * ADMIN_NOTES_MID;
        }
      }

      const pct = (surviving / TOTAL_CIRCULATION) * 100;
      points.push({ year: t, pct: pct });
    }

    return points;
  }

  function draw(lifespan) {
    // Theme-aware colors
    const isDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
    const textColor = isDark ? '#ccc' : '#333';
    const gridColor = isDark ? 'rgba(255,255,255,0.1)' : 'rgba(0,0,0,0.08)';
    const lineColor = '#e44';
    const fillColor = isDark ? 'rgba(228, 68, 68, 0.15)' : 'rgba(228, 68, 68, 0.12)';
    const adminBand = isDark ? 'rgba(228, 68, 68, 0.08)' : 'rgba(228, 68, 68, 0.06)';
    const bgColor = isDark ? '#1d1e20' : '#fff';

    // Clear
    ctx.fillStyle = bgColor;
    ctx.fillRect(0, 0, W, H);

    const pad = { top: 30, right: 20, bottom: 45, left: 55 };
    const plotW = W - pad.left - pad.right;
    const plotH = H - pad.top - pad.bottom;

    const points = computeCurve(lifespan);
    const maxPct = Math.max(...points.map(p => p.pct));
    const yMax = Math.ceil(maxPct / 5) * 5 || 30;

    function x(year) { return pad.left + ((year - CHART_START) / YEARS) * plotW; }
    function y(pct) { return pad.top + plotH - (pct / yMax) * plotH; }

    // Admin printing band
    ctx.fillStyle = adminBand;
    ctx.fillRect(x(PRINT_START), pad.top, x(PRINT_END) - x(PRINT_START), plotH);

    // Grid lines
    ctx.strokeStyle = gridColor;
    ctx.lineWidth = 1;
    const yTicks = 5;
    ctx.font = '11px system-ui, sans-serif';
    ctx.fillStyle = textColor;
    ctx.textAlign = 'right';
    for (let i = 0; i <= yTicks; i++) {
      const pct = (yMax / yTicks) * i;
      const yy = y(pct);
      ctx.beginPath();
      ctx.moveTo(pad.left, yy);
      ctx.lineTo(W - pad.right, yy);
      ctx.stroke();
      ctx.fillText(pct.toFixed(0) + '%', pad.left - 8, yy + 4);
    }

    // X axis labels
    ctx.textAlign = 'center';
    for (let yr = CHART_START; yr <= CHART_END; yr += 5) {
      const xx = x(yr);
      ctx.fillText(yr.toString(), xx, H - pad.bottom + 20);
      ctx.beginPath();
      ctx.moveTo(xx, pad.top);
      ctx.lineTo(xx, pad.top + plotH);
      ctx.stroke();
    }

    // Fill under curve
    ctx.beginPath();
    ctx.moveTo(x(points[0].year), y(0));
    for (const p of points) {
      ctx.lineTo(x(p.year), y(p.pct));
    }
    ctx.lineTo(x(points[points.length - 1].year), y(0));
    ctx.closePath();
    ctx.fillStyle = fillColor;
    ctx.fill();

    // Line
    ctx.beginPath();
    ctx.strokeStyle = lineColor;
    ctx.lineWidth = 2.5;
    ctx.lineJoin = 'round';
    for (let i = 0; i < points.length; i++) {
      const p = points[i];
      if (i === 0) ctx.moveTo(x(p.year), y(p.pct));
      else ctx.lineTo(x(p.year), y(p.pct));
    }
    ctx.stroke();

    // Peak annotation
    const peak = points.reduce((a, b) => a.pct > b.pct ? a : b);
    ctx.fillStyle = textColor;
    ctx.font = 'bold 12px system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('peak ' + peak.pct.toFixed(1) + '%', x(peak.year), y(peak.pct) - 12);

    // Admin label
    ctx.save();
    ctx.fillStyle = isDark ? 'rgba(228,68,68,0.4)' : 'rgba(228,68,68,0.3)';
    ctx.font = '10px system-ui, sans-serif';
    ctx.textAlign = 'center';
    const bandMid = (x(PRINT_START) + x(PRINT_END)) / 2;
    ctx.fillText('printing period', bandMid, pad.top + 16);
    ctx.restore();

    // Axis label
    ctx.save();
    ctx.fillStyle = textColor;
    ctx.font = '12px system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.translate(14, pad.top + plotH / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText('% of all bills in circulation', 0, 0);
    ctx.restore();
  }

  slider.addEventListener('input', function() {
    const v = parseFloat(this.value);
    lifespanLabel.textContent = v % 1 === 0 ? v.toFixed(0) : v.toFixed(1);
    draw(v);
  });

  // Redraw on theme change
  if (window.matchMedia) {
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', function() {
      draw(parseFloat(slider.value));
    });
  }

  // Resize handler
  window.addEventListener('resize', function() {
    // Recalculate on resize for responsiveness
    draw(parseFloat(slider.value));
  });

  draw(8);
})();
</script>

By 2036 or so, roughly 8–10% of all bills in your wallet will still be
ones printed during this window. That share keeps declining but never
drops to zero instantly. It's an exponential decay. Crank the slider
to 24 years (the $100 bill lifespan) and you'll see those Benjamins
hanging around deep into the second half of the century.

## References

- [Federal Reserve Board — 2025 Currency Print Order](https://www.federalreserve.gov/paymentsystems/2025_currency_print_order.htm)
- [Federal Reserve Board — 2026 Currency Print Order](https://www.federalreserve.gov/paymentsystems/files/currency_print_orders_2026.pdf)
- [Federal Reserve Board — Currency Print Orders (main page)](https://www.federalreserve.gov/paymentsystems/coin_currency_orders.htm)
- [Federal Reserve Board — How much does it cost to produce currency and coin?](https://www.federalreserve.gov/faqs/currency_12771.htm)
- [Bureau of Engraving and Printing — FY 2025 Congressional Justification](https://home.treasury.gov/system/files/266/19.-BEP-FY-2025-CJ.pdf)
- [Numismatic News — Federal Reserve Board Releases 2025 Figures](https://www.numismaticnews.net/paper-money-market-federal-reserve-board-releases-2025-figures)
