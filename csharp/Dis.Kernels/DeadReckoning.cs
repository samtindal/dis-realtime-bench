using System.Runtime.CompilerServices;

namespace Dis.Kernels;

public static class DeadReckoning
{
    public const double Dt = 0.05;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static D3 MatVec(in Mat3 a, in D3 v)
    {
        D3 o = default;
        for (int i = 0; i < 3; i++)
            o[i] = a[i * 3] * v[0] + a[i * 3 + 1] * v[1] + a[i * 3 + 2] * v[2];
        return o;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static Mat3 DcmFromEuler(double psi, double theta, double phi)
    {
        double cps = Math.Cos(psi), sps = Math.Sin(psi);
        double cth = Math.Cos(theta), sth = Math.Sin(theta);
        double cph = Math.Cos(phi), sph = Math.Sin(phi);
        Mat3 r = default;
        r[0] = cth * cps;
        r[1] = cth * sps;
        r[2] = -sth;
        r[3] = sph * sth * cps - cph * sps;
        r[4] = sph * sth * sps + cph * cps;
        r[5] = sph * cth;
        r[6] = cph * sth * cps + sph * sps;
        r[7] = cph * sth * sps - sph * cps;
        r[8] = cph * cth;
        return r;
    }

    /// <summary>DRM_RVB: body-frame velocity and acceleration, rotating. P(t) = P0 + R0^T (R1 V0 + R2 A0).</summary>
    public static void DrmRvb(in D3 p0, in D3 v0, in D3 a0, in D3 w,
                              double psi, double theta, double phi, double t, out D3 result)
    {
        Mat3 r0 = DcmFromEuler(psi, theta, phi);
        double w2 = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
        double wm = Math.Sqrt(w2);

        Mat3 r1 = default, r2 = default;
        if (wm < 1e-8)
        {
            // Series limits: R1 -> t*I, R2 -> (t^2/2)*I. Avoids the w^3 / w^4 divisions.
            for (int i = 0; i < 3; i++)
            {
                r1[i * 3 + i] = t;
                r2[i * 3 + i] = 0.5 * t * t;
            }
        }
        else
        {
            double wt = wm * t;
            double s = Math.Sin(wt), c = Math.Cos(wt);
            double w3 = w2 * wm, w4 = w2 * w2;

            double c1I = s / wm;
            double c1Sk = (1.0 - c) / w2;
            double c1Op = (wt - s) / w3;

            double c2I = (c + wt * s - 1.0) / w2;
            double c2Sk = (s - wt * c) / w3;
            double c2Op = (0.5 * w2 * t * t + c - 1.0) / w4;

            Mat3 sk = default, op = default;
            sk[1] = -w[2]; sk[2] = w[1]; sk[3] = w[2]; sk[5] = -w[0]; sk[6] = -w[1]; sk[7] = w[0];
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    op[i * 3 + j] = w[i] * w[j];
            for (int i = 0; i < 9; i++)
            {
                double id = i % 4 == 0 ? 1.0 : 0.0;
                r1[i] = c1I * id + c1Sk * sk[i] + c1Op * op[i];
                r2[i] = c2I * id + c2Sk * sk[i] + c2Op * op[i];
            }
        }

        D3 tv = MatVec(in r1, in v0);
        D3 ta = MatVec(in r2, in a0);
        D3 sum = new(tv[0] + ta[0], tv[1] + ta[1], tv[2] + ta[2]);

        Mat3 r0t = default; // R0^T
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                r0t[i * 3 + j] = r0[j * 3 + i];

        D3 rot = MatVec(in r0t, in sum);
        result = new D3(p0[0] + rot[0], p0[1] + rot[1], p0[2] + rot[2]);
    }
}
