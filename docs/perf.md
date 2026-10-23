# Performance log

How to measure: `photobrowse <folder> --bench` runs a scripted 10.5 s tour in
fullscreen (zoom out to the whole grid, two flings, zoom into detail, whole-photo
view, step ×3, leave) and prints frame-interval percentiles. Frames with > 20 ms of
CPU work are listed with a phase breakdown. `PB_WORKERS=<n>` overrides the number of
streaming decode threads. Build with the `linux-release` preset.

"Frame interval" is the time between successive `SwapWindow` returns. With vsync at
60 Hz it is 16.7 ms ± ~1 ms of measurement jitter; a dropped frame shows as ~33 ms,
so ">20 ms" counts real hitches.

## 2026-10-21 — v1

Machine: laptop, Intel UHD 630 (CML GT2, Mesa 25.2.8), 3840×2160 @ 60 Hz panel,
16 threads, NVMe. Fullscreen 4K. Libraries: the 20 sample photos; a 10,000-photo
synthetic library (`tools/make_test_library.py`, symlinks to the samples, so disk
reads are served from the OS cache).

| Scenario | p50 | p95 | p99 | max | frames > 20 ms |
|---|---|---|---|---|---|
| 20 photos, warm cache (4 runs) | 16.7 | 17.5–17.6 | 17.9–18.1 | 18.4–20.8 | 0–1 of ~628 |
| 10k photos, warm cache (5 runs) | 16.7 | 17.5–17.6 | 17.8–18.1 | 18.3–33.2 | 0–2 of ~620 |
| 10k photos, L2–L4 cache cleared (3 runs) | 16.7 | 17.5–17.7 | 17.9–18.0 | 18.2–18.6 | 0 |

- Warm start, 10k photos: first full frame ~140 ms after the window opens.
- Cold first run, 10k photos: complete grid after 7.2 s (75% of photos use the
  embedded preview; the rest are 1/8-scale decodes).
- Cache for 10k: catalog 0.8 MB, `l0.pack` 40 MB, L1 73 MB.
- GPU memory in use during the bench: 171 MB (20 photos) / 341 MB (10k) of 640 MB.

### What fixed the hitches (for future reference)
1. **Page texture allocation** (~126 ms each on Mesa/iris): page textures are pooled
   and pre-allocated one per idle frame (`GpuTextures::prewarm`).
2. **Full-resolution uploads** (14–31 MB in one call): fixed-capacity full textures
   (display-sized, pre-allocated) filled a few rows per frame within the upload budget.
3. **Images larger than the full-texture capacity** forced reallocations (15–60 ms):
   full decodes are clamped to the capacity (`Streamer::fitFullCapacity`).
4. **Per-call driver overhead** with many small uploads: uploads go through a ring of
   persistent pixel-unpack buffers (`UploadRing`) and are capped by a 3 ms time
   budget in addition to the 4 MB byte budget.
5. **Decode threads competing with the integrated GPU / render thread**: browsing uses
   a small pool (2–4 workers, `streamingWorkerCount`); first-run indexing uses all
   cores (no browsing happens until it finishes). Workers run at lowered priority.

6. **Eviction thrash** (found in review): tiles visible last frame could be evicted
   to place a new load, so a level whose slots were full kept reloading forever.
   Entries seen in the last two frames are now protected, and the same rule applies
   to the 3 full-resolution slots. This also removed the occasional ~30 ms frame.

### After review fixes (same machine, 4K fullscreen)

| Scenario | p50 | p95 | p99 | max | frames > 20 ms |
|---|---|---|---|---|---|
| 10k photos (3 runs) | 16.7 | 17.6 | 18.0–18.1 | 18.5–19.3 | 0 |
| 20 photos (2 runs) | 16.7 | 17.6 | 18.0–18.1 | 18.2–18.8 | 0 |

No dropped frames. The measured p99 interval (≈18 ms) is swap-timing jitter around
the 16.7 ms vsync, not missed frames.

### Integrated vs discrete GPU (2026-10-21, 10k photos, fullscreen 4K)

| GPU | background | p50 / p99 interval | max | CPU work p99 |
|---|---|---|---|---|
| Intel UHD 630 | solid | 16.7 / 18.0–18.2 ms | 18.7 ms | 9–11 ms |
| Intel UHD 630 | aurora | 16.7 / 17.9–18.3 ms | 20.0 ms | 11–16 ms |
| RTX 2080 Super Max-Q (PRIME offload) | solid | 16.7 / 17.9–18.1 ms | 24 ms (1–2 frames) | 6.3 ms |
| RTX 2080 Super Max-Q (PRIME offload) | aurora | 16.7 / 18.1 ms | 20.2 ms (1–2 frames) | 6.2–6.6 ms |

Both reach the 60 Hz panel's limit. The NVIDIA GPU halves CPU time per frame and has
far more GPU headroom (useful for high-refresh monitors or heavy shaders); its few
20–24 ms frames are likely the PRIME copy to the Intel-driven panel. Select it with
`[display] gpu = discrete`.

### Background shader GPU cost (2026-10-21, Intel UHD 630, fullscreen 4K)

`--bench` now also reports the background pass's GPU time (GL timer query).
Budget: the whole frame must fit in 16.7 ms; keep backgrounds around 4–5 ms (p50)
on this GPU. A do-nothing animated shader costs 1.7 ms (pure 4K fill).

| Preset | p50 | p99 |
|---|---|---|
| gradient / vignette / spotlight / waves | 2.3–2.5 ms | 4.0 ms |
| dots / grain | 2.6–2.9 ms | 4.5–5.7 ms |
| aurora | 4.2 ms | 7.7 ms |
| starfield | 4.3 ms | 7.3 ms |
| warp | 4.4 ms | 7.5–8.8 ms |
| bokeh | 4.5 ms | 7.4 ms |
| nebula | 6.6 ms | 9.0 ms (occasional dropped frame) |

Warp went from 6.4 to 4.4 ms by skipping the streak math for pixels that are not
on a streak (early `continue`), fewer layers and a cheaper hash; replacing `atan`
saved only ~0.5 ms. Measure before optimizing shaders.

## 2026-10-22 — Focused-photo detail zoom

Paired release benchmarks before and after the focused-photo 1×–4× zoom change.
RTX 2080 Super Max-Q (PRIME offload), 3840×2160 fullscreen, animated warp
background, 288-photo warm library, two runs each.

| Build | p50 | p95 | p99 | max | frames > 20 ms | CPU work p99 |
|---|---|---|---|---|---|---|
| Before | 16.68 | 17.60–17.71 | 18.03–18.08 | 18.61–19.09 | 0 | 1.68–1.91 |
| After | 16.67 | 17.67–17.69 | 18.11–18.35 | 19.28–21.27 | 0–2 of ~629 | 1.93–2.15 |

The focused photo and its neighbours now decode at the sharpest size that fits the
already allocated full-texture capacity. Frame pacing stayed at the 60 Hz limit;
one after-run had two marginal 20–21 ms intervals, with CPU work peaking at only
4.31 ms (consistent with the existing PRIME/display-copy variance rather than a
decode or upload stall). Warm-run reported residency was 341 MB before and after.

## 2026-10-22 — Tile alpha (screensaver photo_only)

The tile shader gained a per-instance alpha (vec3 params instead of vec2); blending
is enabled only for a draw whose tiles include alpha < 1. Paired release benchmarks,
RTX 2080 Super Max-Q, 3840×2160 fullscreen, 288-photo warm library, two runs each.

| Build | p50 | p95 | p99 | max | frames > 20 ms | CPU work p99 |
|---|---|---|---|---|---|---|
| Before | 16.67 | 17.65 | 18.03–18.08 | 19.65–23.26 | 0–3 of ~630 | 2.15–2.22 |
| After | 16.66–16.68 | 17.57–17.62 | 18.17–18.18 | 20.41–20.78 | 1–2 of ~630 | 1.80–2.14 |

No measurable difference; the few > 20 ms intervals appear in both builds (the known
PRIME/display-copy variance; CPU work stayed under 4 ms after).

## 2026-10-22 — Rounded corners (`[view] corner_radius`)

The tile fragment shader computes a rounded-rectangle signed distance (a few ALU ops,
one pixel of anti-aliasing); with a radius > 0 tile draws blend. Release builds,
3840×2160 fullscreen, 288-photo warm library, two runs each.

| GPU | Build / setting | p50 | p95 | p99 | frames > 20 ms |
|---|---|---|---|---|---|
| RTX 2080 Super Max-Q | before | 16.67 | 17.60–17.61 | 18.06–18.08 | 2, 2 |
| RTX 2080 Super Max-Q | after, radius 0 | 16.65–16.67 | 17.61–17.63 | 18.05–18.33 | 1, 1 |
| RTX 2080 Super Max-Q | after, radius 0.08 | 16.66–16.67 | 17.62 | 18.01–18.03 | 1, 1 |
| Intel UHD (CML GT2) | after, radius 0 | 16.67–16.70 | 17.67–17.70 | 18.07–18.09 | 2, 2 |
| Intel UHD (CML GT2) | after, radius 0.08 | 16.69 | 17.57–17.59 | 17.93–18.05 | 1, 0 |

No measurable cost, including on the integrated reference GPU with blending on.
