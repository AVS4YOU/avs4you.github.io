//---------------------------------------------------------------------------
/**
 * @file effect_common.h
 * @brief Header-only helpers shared by AVS4YOU Image/Video effect plugins.
 *
 * Everything here is optional: an effect plugin only needs the exports listed
 * in CEffectPluginIntf.h. These helpers exist so that every effect behaves the
 * same way in the three places that matter most:
 *
 *   1. TIME    - NSEffect::CClock turns the host's dCompleteness argument into
 *                a monotonic, seconds-like clock, so animation is continuous
 *                from frame to frame instead of being re-randomised per frame.
 *   2. RANDOM  - NSEffect::Hash* are stateless integer hashes. Give a particle
 *                a fixed id and its "random" properties stay identical on
 *                every frame - that is what makes an animation coherent.
 *   3. PIXELS  - NSEffect::CSurface wraps the BGRA buffer the host hands over
 *                and offers clamped, anti-aliased splatting and sampling.
 */

#ifndef AVS_EFFECT_COMMON_H
#define AVS_EFFECT_COMMON_H
//---------------------------------------------------------------------------

#include <windows.h>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace NSEffect
{
    // ======================================================================
    // Small math helpers
    // ======================================================================

    inline float Clampf(float v, float lo, float hi)
    {
        return (v < lo) ? lo : ((v > hi) ? hi : v);
    }

    inline int Clampi(int v, int lo, int hi)
    {
        return (v < lo) ? lo : ((v > hi) ? hi : v);
    }

    inline BYTE ClampByte(float v)
    {
        return (BYTE)((v < 0.0f) ? 0 : ((v > 255.0f) ? 255 : (int)(v + 0.5f)));
    }

    inline float Fract(float v)
    {
        return v - floorf(v);
    }

    /** Wraps v into [0, size). Safe for negative v. */
    inline float Wrapf(float v, float size)
    {
        if (size <= 0.0f)
            return 0.0f;

        const float r = fmodf(v, size);
        return (r < 0.0f) ? (r + size) : r;
    }

    inline float Lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    /** Classic smoothstep on an already normalised t. */
    inline float Smoothstep(float t)
    {
        t = Clampf(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // ======================================================================
    // Stateless hashes and noise
    // ----------------------------------------------------------------------
    // Deterministic: the same seed always yields the same value, on every
    // frame and on every machine. Never use rand() for per-particle
    // properties - it makes the animation flicker.
    // ======================================================================

    inline unsigned int HashU32(unsigned int x)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    inline unsigned int HashU32(unsigned int x, unsigned int y)
    {
        return HashU32(x ^ (y * 0x9e3779b9U));
    }

    /** Uniform float in [0, 1). */
    inline float Rand01(unsigned int seed)
    {
        return (float)(HashU32(seed) >> 8) * (1.0f / 16777216.0f);
    }

    /** Uniform float in [-1, 1). */
    inline float Rand11(unsigned int seed)
    {
        return Rand01(seed) * 2.0f - 1.0f;
    }

    /** Uniform float in [lo, hi). */
    inline float RandRange(unsigned int seed, float lo, float hi)
    {
        return lo + (hi - lo) * Rand01(seed);
    }

    /** Smooth 1D value noise in [-1, 1]. Continuous in x, so safe for time. */
    inline float Noise1D(float x, unsigned int seed)
    {
        const float fx = floorf(x);
        const int   i = (int)fx;
        const float t = Smoothstep(x - fx);

        const float a = Rand11((unsigned int)(i)*0x27d4eb2dU + seed);
        const float b = Rand11((unsigned int)(i + 1) * 0x27d4eb2dU + seed);
        return Lerp(a, b, t);
    }

    /** Two octaves of Noise1D, still in [-1, 1]. */
    inline float Noise1D2(float x, unsigned int seed)
    {
        return Noise1D(x, seed) * 0.66f + Noise1D(x * 2.17f + 11.3f, seed ^ 0x5bf03635U) * 0.34f;
    }

    /** Per-pixel static hash noise in [-1, 1], stable for a given frame index. */
    inline float NoisePixel(int x, int y, unsigned int frame)
    {
        return Rand11(HashU32((unsigned int)x * 0x1f123bb5U + (unsigned int)y * 0x9e3779b9U, frame));
    }

    // ======================================================================
    // CClock - temporal coherence
    // ----------------------------------------------------------------------
    // The host passes one time-like scalar into ApplyEffect (dCompleteness).
    // Depending on host and application that value is either
    //   * a 0..1 progress ratio through the effect, or
    //   * a timestamp in seconds.
    //
    // CClock does not care which: it measures the average step between two
    // consecutive calls and rebuilds a frame index from it, so
    //
    //     Time() == frameIndex / NominalFps()
    //
    // in both cases. Motion therefore runs at the same speed no matter how
    // long the clip is, and stays continuous across frames.
    //
    // Usage inside ApplyEffect:
    //
    //     CClock* clock = ...;              // stored in **effectData
    //     clock->Tick(dCompleteness);
    //     const float t = (float)clock->Time();
    //
    // Express motion as a closed-form function of t (position = f(t)) rather
    // than as an incremental "position += step" update: a closed form survives
    // seeking, scrubbing and out-of-order frame rendering, an accumulator
    // does not.
    // ======================================================================

    class CClock
    {
    public:
        /** Frame rate assumed when the host gives no better information. */
        static double NominalFps() { return 25.0; }

        CClock()
            : m_time(0.0)
            , m_prev(-1.0)
            , m_stepEma(0.0)
            , m_frame(0)
            , m_seeked(false)
        {
        }

        void Reset()
        {
            m_time = 0.0;
            m_prev = -1.0;
            m_stepEma = 0.0;
            m_frame = 0;
            m_seeked = false;
        }

        /** Call exactly once per ApplyEffect invocation, before drawing. */
        void Tick(double completeness)
        {
            const double dt = 1.0 / NominalFps();

            if (m_prev < 0.0)
            {
                // First frame of this effect instance.
                m_time = 0.0;
                m_seeked = false;
            }
            else
            {
                const double step = completeness - m_prev;

                // Learn the per-frame step. Ignore non-advancing calls (the
                // preview tool can pin the value) and absurd jumps (a seek).
                if (step > 1e-9 && step < 0.5)
                    m_stepEma = (m_stepEma <= 0.0) ? step : (m_stepEma * 0.85 + step * 0.15);

                const double linear = m_time + dt;

                if (m_stepEma > 0.0)
                {
                    // Rebuild absolute time from the host value. Works whether
                    // completeness is a 0..1 ratio or a seconds timestamp.
                    const double absolute = (completeness / m_stepEma) * dt;

                    if (fabs(absolute - linear) > 0.5)
                    {
                        m_time = absolute;   // seek / scrub / new render pass
                        m_seeked = true;
                    }
                    else
                    {
                        m_time = linear;
                        m_seeked = false;
                    }
                }
                else
                {
                    m_time = linear;
                    m_seeked = false;
                }
            }

            m_prev = completeness;
            ++m_frame;
        }

        /** Monotonic time in seconds. */
        double Time() const { return m_time; }

        /** Nominal time between two frames, in seconds. */
        double Delta() const { return 1.0 / NominalFps(); }

        /** Number of ApplyEffect calls seen so far (1 after the first Tick). */
        unsigned int Frame() const { return m_frame; }

        /** True when the last Tick had to jump (seek, scrub, restart). */
        bool Seeked() const { return m_seeked; }

    private:
        double       m_time;
        double       m_prev;
        double       m_stepEma;
        unsigned int m_frame;
        bool         m_seeked;
    };

    // ======================================================================
    // CSurface - BGRA frame access
    // ----------------------------------------------------------------------
    // Channel order is B, G, R, A (4 bytes per pixel, rows top to bottom).
    // ======================================================================

    class CSurface
    {
    public:
        CSurface(BYTE* pixels, int width, int height)
            : m_p(pixels), m_w(width), m_h(height)
        {
        }

        BYTE*  Data() const { return m_p; }
        int    Width() const { return m_w; }
        int    Height() const { return m_h; }
        size_t SizeInBytes() const { return (size_t)m_w * (size_t)m_h * 4; }

        BYTE* At(int x, int y) const
        {
            return m_p + ((size_t)y * (size_t)m_w + (size_t)x) * 4;
        }

        bool Inside(int x, int y) const
        {
            return (x >= 0 && y >= 0 && x < m_w && y < m_h);
        }

        /** Alpha blend a colour over one pixel. No bounds check. */
        void BlendPixel(int x, int y, float b, float g, float r, float a) const
        {
            BYTE* px = At(x, y);
            px[0] = ClampByte(px[0] + (b - px[0]) * a);
            px[1] = ClampByte(px[1] + (g - px[1]) * a);
            px[2] = ClampByte(px[2] + (r - px[2]) * a);
        }

        /** Additive contribution to one pixel. No bounds check. */
        void AddPixel(int x, int y, float b, float g, float r, float a) const
        {
            BYTE* px = At(x, y);
            px[0] = ClampByte(px[0] + b * a);
            px[1] = ClampByte(px[1] + g * a);
            px[2] = ClampByte(px[2] + r * a);
        }

        /**
         * Anti-aliased round splat, alpha blended.
         * radius is in pixels, alpha is the peak opacity at the centre.
         *
         * The falloff is 1 - (d/radius)^2, which is why nothing here takes a
         * square root: the squared distance is all the weight needs. Splats are
         * the hot loop of every particle effect, so keep it that way.
         */
        void SplatBlend(float cx, float cy, float radius, float b, float g, float r, float alpha) const
        {
            if (alpha <= 0.002f || radius <= 0.0f)
                return;

            const int x0 = Clampi((int)floorf(cx - radius), 0, m_w - 1);
            const int x1 = Clampi((int)ceilf(cx + radius), 0, m_w - 1);
            const int y0 = Clampi((int)floorf(cy - radius), 0, m_h - 1);
            const int y1 = Clampi((int)ceilf(cy + radius), 0, m_h - 1);

            if (x1 < x0 || y1 < y0)
                return;

            const float inv2 = 1.0f / (radius * radius);

            for (int y = y0; y <= y1; ++y)
            {
                const float dy = ((float)y + 0.5f) - cy;
                const float dy2 = dy * dy;

                for (int x = x0; x <= x1; ++x)
                {
                    const float dx = ((float)x + 0.5f) - cx;
                    const float d2 = (dx * dx + dy2) * inv2;

                    if (d2 >= 1.0f)
                        continue;

                    BlendPixel(x, y, b, g, r, alpha * (1.0f - d2));
                }
            }
        }

        /** Anti-aliased round splat, additive. */
        void SplatAdd(float cx, float cy, float radius, float b, float g, float r, float alpha) const
        {
            if (alpha <= 0.002f || radius <= 0.0f)
                return;

            const int x0 = Clampi((int)floorf(cx - radius), 0, m_w - 1);
            const int x1 = Clampi((int)ceilf(cx + radius), 0, m_w - 1);
            const int y0 = Clampi((int)floorf(cy - radius), 0, m_h - 1);
            const int y1 = Clampi((int)ceilf(cy + radius), 0, m_h - 1);

            if (x1 < x0 || y1 < y0)
                return;

            const float inv2 = 1.0f / (radius * radius);

            for (int y = y0; y <= y1; ++y)
            {
                const float dy = ((float)y + 0.5f) - cy;
                const float dy2 = dy * dy;

                for (int x = x0; x <= x1; ++x)
                {
                    const float dx = ((float)x + 0.5f) - cx;
                    const float d2 = (dx * dx + dy2) * inv2;

                    if (d2 >= 1.0f)
                        continue;

                    AddPixel(x, y, b, g, r, alpha * (1.0f - d2));
                }
            }
        }

        /**
         * Anti-aliased soft line from (x0,y0) to (x1,y1).
         * alphaHead / alphaTail let a motion-blurred streak fade along itself.
         *
         * stepScale trades quality for speed by spacing the splats further
         * apart. 1.0 is seamless; a soft, wide, low-alpha pass (a blur halo, an
         * out-of-focus streak) is fine at 1.5-2.0 and costs proportionally less.
         */
        void StreakBlend(float x0, float y0, float x1, float y1, float radius,
                         float b, float g, float r,
                         float alphaHead, float alphaTail, float stepScale = 1.0f) const
        {
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float len = sqrtf(dx * dx + dy * dy);

            if (len < 0.0001f)
            {
                SplatBlend(x0, y0, radius, b, g, r, alphaHead);
                return;
            }

            const float step = ((radius > 1.0f) ? (radius * 0.6f) : 0.8f) * stepScale;
            const int   count = (int)(len / step) + 1;
            const float invCount = 1.0f / (float)count;

            for (int i = 0; i <= count; ++i)
            {
                const float t = (float)i * invCount;
                SplatBlend(x0 + dx * t, y0 + dy * t, radius,
                           b, g, r, Lerp(alphaHead, alphaTail, t));
            }
        }

        /** Bilinear sample, out receives B,G,R,A. Coordinates are clamped. */
        void SampleBilinear(float x, float y, float* out) const
        {
            x = Clampf(x, 0.0f, (float)(m_w - 1));
            y = Clampf(y, 0.0f, (float)(m_h - 1));

            const int   xi = (int)x;
            const int   yi = (int)y;
            const int   xn = (xi + 1 < m_w) ? (xi + 1) : xi;
            const int   yn = (yi + 1 < m_h) ? (yi + 1) : yi;
            const float fx = x - (float)xi;
            const float fy = y - (float)yi;

            const BYTE* p00 = At(xi, yi);
            const BYTE* p10 = At(xn, yi);
            const BYTE* p01 = At(xi, yn);
            const BYTE* p11 = At(xn, yn);

            for (int c = 0; c < 4; ++c)
            {
                const float top = Lerp((float)p00[c], (float)p10[c], fx);
                const float bot = Lerp((float)p01[c], (float)p11[c], fx);
                out[c] = Lerp(top, bot, fy);
            }
        }

    private:
        BYTE* m_p;
        int   m_w;
        int   m_h;
    };

    // ======================================================================
    // Parameter reading
    // ----------------------------------------------------------------------
    // Hosts that support GetEffectParams pass the values positionally through
    // ApplyEffect's (paramCount, params) pair. Hosts that do not simply pass
    // (0, NULL) - so always fall back to a sane default.
    // ======================================================================

    inline double ParamDouble(int paramCount, const BSTR* params, int index, double fallback)
    {
        if (!params || index < 0 || index >= paramCount || !params[index])
            return fallback;

        wchar_t* end = NULL;
        const double value = wcstod(params[index], &end);

        if (end == (wchar_t*)params[index])
            return fallback;

        return value;
    }

    inline float ParamFloat(int paramCount, const BSTR* params, int index, float fallback)
    {
        return (float)ParamDouble(paramCount, params, index, (double)fallback);
    }

    inline int ParamInt(int paramCount, const BSTR* params, int index, int fallback)
    {
        return (int)ParamDouble(paramCount, params, index, (double)fallback);
    }

    inline bool ParamBool(int paramCount, const BSTR* params, int index, bool fallback)
    {
        return ParamDouble(paramCount, params, index, fallback ? 1.0 : 0.0) >= 0.5;
    }

    // ======================================================================
    // String export helpers - every wchar_t* returned to the host must be
    // allocated here and released in ReleasePluginString.
    // ======================================================================

    inline wchar_t* ExportString(const wchar_t* value)
    {
        if (value == NULL)
            return NULL;

        const size_t len = wcslen(value);
        wchar_t* result = new wchar_t[len + 1];
        wcscpy_s(result, len + 1, value);
        return result;
    }

    inline void ReleaseString(wchar_t* value)
    {
        delete[] value;
    }
}

#endif // AVS_EFFECT_COMMON_H
