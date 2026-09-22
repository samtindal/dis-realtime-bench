namespace Dis.Kernels;

/// <summary>
/// Canonical text rendering used for cross-language equivalence (scripts/verify.py).
/// Floats are printed as IEEE-754 bit patterns so "equal" means bit-identical.
/// </summary>
public static class Dump
{
    private static string H(float v) => BitConverter.SingleToUInt32Bits(v).ToString("x8");
    private static string H(double v) => BitConverter.DoubleToUInt64Bits(v).ToString("x16");

    public static void Write(TextWriter w)
    {
        byte[] c = Corpus.Build();
        w.Write($"corpus_fnv1a64 {Corpus.Fnv1a64(c):x16}\n");
        Variant<Idiomatic>(w, c);
        Variant<Shift>(w, c);

        DrInputs d = Corpus.BuildDrInputs();
        for (int i = 0; i < Corpus.Count; i++)
        {
            DeadReckoning.DrmRvb(in d.P0[i], in d.V0[i], in d.A0[i], in d.W[i],
                                 d.Eul[i][0], d.Eul[i][1], d.Eul[i][2], DeadReckoning.Dt, out D3 o);
            w.Write($"dr {i} {H(o[0])} {H(o[1])} {H(o[2])}\n");
        }
        // Displacement only (P0 = 0). The full outputs above are ~1e6 m, so a last-bit
        // difference in the ~0.5 m displacement would be rounded away by the final addition.
        D3 origin = default;
        for (int i = 0; i < Corpus.Count; i++)
        {
            DeadReckoning.DrmRvb(in origin, in d.V0[i], in d.A0[i], in d.W[i],
                                 d.Eul[i][0], d.Eul[i][1], d.Eul[i][2], DeadReckoning.Dt, out D3 o);
            w.Write($"dr_disp {i} {H(o[0])} {H(o[1])} {H(o[2])}\n");
        }
        D3 zeroW = default;
        DeadReckoning.DrmRvb(in d.P0[0], in d.V0[0], in d.A0[0], in zeroW,
                             d.Eul[0][0], d.Eul[0][1], d.Eul[0][2], DeadReckoning.Dt, out D3 z);
        w.Write($"dr_zero {H(z[0])} {H(z[1])} {H(z[2])}\n");
    }

    private static void Variant<L>(TextWriter w, byte[] c) where L : struct, ILoad
    {
        string name = L.Name;
        for (int i = 0; i < Corpus.Count; i++)
        {
            var e = new EntityState();
            if (!Espdu.Decode<L>(c.AsSpan(i * Espdu.Size, Espdu.Size), ref e))
            {
                w.Write($"pdu {name} {i} DECODE_FAILED\n");
                continue;
            }
            w.Write($"pdu {name} {i} {e.Site} {e.Application} {e.Entity} {e.ForceId} {e.Kind} {e.Domain} {e.Country}");
            for (int k = 0; k < 3; k++) w.Write($" {H(e.Velocity[k])}");
            for (int k = 0; k < 3; k++) w.Write($" {H(e.Location[k])}");
            for (int k = 0; k < 3; k++) w.Write($" {H(e.Orientation[k])}");
            w.Write($" {e.Appearance:x8} {e.DrAlgorithm}");
            for (int k = 0; k < 3; k++) w.Write($" {H(e.LinAccel[k])}");
            for (int k = 0; k < 3; k++) w.Write($" {H(e.AngVel[k])}");
            w.Write(' ');
            for (int k = 0; k < 11; k++) w.Write($"{e.Marking[k]:x2}");
            w.Write($" {e.Capabilities:x8}\n");
        }

        ReadOnlySpan<byte> full = c.AsSpan(0, Espdu.Size);
        var s = new EntityState();
        int truncated = 0;
        for (int n = 0; n < Espdu.Size; n++) truncated += Espdu.Decode<L>(full[..n], ref s) ? 1 : 0;
        int fullOk = Espdu.Decode<L>(full, ref s) ? 1 : 0;
        byte[] bad = full.ToArray();
        bad[2] = 2;
        int badType = Espdu.Decode<L>(bad, ref s) ? 1 : 0;
        byte[] shortLen = full.ToArray();
        shortLen[9] = 143;
        int shortLength = Espdu.Decode<L>(shortLen, ref s) ? 1 : 0;
        w.Write($"edge {name} truncated_accepts {truncated}\n");
        w.Write($"edge {name} full_accepts {fullOk}\n");
        w.Write($"edge {name} bad_type_accepts {badType}\n");
        w.Write($"edge {name} short_length_accepts {shortLength}\n");
    }
}
