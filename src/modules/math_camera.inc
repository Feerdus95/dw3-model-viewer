/* --- small matrix math, column-major --- */

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


