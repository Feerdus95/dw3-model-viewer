/* --- small matrix math, column-major --- */

struct M4 {
    float m[16];
};

static M4 mul(const M4 &a, const M4 &b) {
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            r.m[c * 4 + i] = a.m[i] * b.m[c * 4] + a.m[4 + i] * b.m[c * 4 + 1] + a.m[8 + i] * b.m[c * 4 + 2] + a.m[12 + i] * b.m[c * 4 + 3];
    return r;
}

static M4 from(const float *f) {
    M4 r;
    memcpy(r.m, f, sizeof r.m);
    return r;
}

static M4 perspective(float fovy, float aspect, float n, float f) {
    float t = 1.0f / tanf(fovy / 2);
    M4 r = {};
    r.m[0] = t / aspect, r.m[5] = t, r.m[10] = (f + n) / (n - f), r.m[11] = -1, r.m[14] = 2 * f * n / (n - f);
    return r;
}

/* The camera, orbiting its target; the PSX's y is down and its z away from
   the camera, so the model goes through diag(1, -1, -1) */
struct Camera {
    float yaw = 0.6f, pitch = 0.25f, dist = 1500, target[3] = {0, 300, 0};
    M4 view() const {
        float e[3] = {target[0] + dist * cosf(pitch) * sinf(yaw), target[1] + dist * sinf(pitch),
                      target[2] + dist * cosf(pitch) * cosf(yaw)};
        float f[3] = {target[0] - e[0], target[1] - e[1], target[2] - e[2]};
        float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (float &x : f) x /= fl;
        float s[3] = {f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0}; /* f x up */
        float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        for (float &x : s) x /= sl;
        float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
        M4 r = {{s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1}};
        r.m[12] = -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]);
        r.m[13] = -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]);
        r.m[14] = f[0] * e[0] + f[1] * e[1] + f[2] * e[2];
        return r;
    }
};

static const M4 PSX_TO_GL = {{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1}};

