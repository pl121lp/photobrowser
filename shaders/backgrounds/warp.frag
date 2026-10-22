#version 410 core
// Warp speed: flying forward through a starfield. Stars appear near the centre,
// accelerate outward with perspective and stretch into streaks as they rush past.
// Each of two layers splits the screen into radial rays carrying one star each,
// so a pixel evaluates at most two stars.
uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;

// Arithmetic hash (no sin): cheap on integrated GPUs.
float hash(float n) {
  n = fract(n * 0.1031);
  n *= n + 33.33;
  n *= n + n;
  return fract(n);
}

// atan2 approximation (max error ~0.0002 rad), much cheaper than atan() on iGPUs.
float fastAtan2(float y, float x) {
  float ax = abs(x), ay = abs(y);
  float a = min(ax, ay) / max(max(ax, ay), 1e-9);
  float s = a * a;
  float r = ((-0.0464964749 * s + 0.15931422) * s - 0.327622764) * s * a + a;
  if (ay > ax) r = 1.57079637 - r;
  if (x < 0.0) r = 3.14159274 - r;
  return y < 0.0 ? -r : r;
}

void main() {
  vec2 uv = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;
  float r = length(uv);
  float angle = fastAtan2(uv.y, uv.x) / 6.2831853 + 0.5;  // 0..1 around the centre
  float px = 1.0 / uResolution.y;                     // one pixel in uv units

  vec3 c = uColor2 + uColor * 0.12 * exp(-r * r * 10.0);  // faint glow ahead
  vec3 starColor = mix(uColor, vec3(1.0), 0.6);
  for (int layer = 0; layer < 2; ++layer) {
    float fl = float(layer);
    float rays = 120.0 + fl * 90.0;
    float s = angle * rays + fl * 0.37;
    float id = floor(s) + fl * 1000.0;
    float h = hash(id);
    if (h < 0.25) continue;                           // leave some rays empty
    float speed = 0.22 + 0.3 * fract(h * 7.13);       // laps per second
    float z = fract(h + uTime * speed);              // 0 = far away, 1 = passing by
    float head = 0.02 + 1.3 * z * z * z;             // perspective: accelerates outward
    float len = 0.003 + 0.2 * z * z;                 // faster stars leave longer streaks
    if (r < head - len || r > head + 2.0 * px) continue;  // most pixels: not on this streak
    float along = smoothstep(head - len, head, r) * (1.0 - smoothstep(head, head + 2.0 * px, r));
    float halfWidth = (0.5 + 1.5 * z) * px;          // nearer stars are thicker
    float across = 1.0 - smoothstep(0.0, halfWidth, abs(fract(s) - 0.5) * 6.2831853 * r / rays);
    float fadeIn = smoothstep(0.0, 0.3, z);
    c += starColor * along * across * fadeIn;
  }
  fragColor = vec4(c, 1.0);
}
