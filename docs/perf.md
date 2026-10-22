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
