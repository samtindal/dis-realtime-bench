using System.Buffers.Binary;

namespace Dis.Kernels;

/// <summary>Dead-reckoning inputs, one 3-vector per entity.</summary>
public sealed class DrInputs
{
    public readonly D3[] P0 = new D3[Corpus.Count];
    public readonly D3[] V0 = new D3[Corpus.Count];
    public readonly D3[] A0 = new D3[Corpus.Count];
    public readonly D3[] W = new D3[Corpus.Count];
    public readonly D3[] Eul = new D3[Corpus.Count];
}

public static class Corpus
{
    public const int Count = 1024; // 147,456 B corpus: stays cache-resident

    /// <summary>Deterministic corpus: every language builds these exact bytes (checked by FNV-1a hash).</summary>
    public static byte[] Build()
    {
        var buf = new byte[Count * Espdu.Size];
        for (int i = 0; i < Count; i++)
        {
            Span<byte> p = buf.AsSpan(i * Espdu.Size, Espdu.Size);
            p[0] = 7; // protocol version 1278.1-2012
            p[1] = 1; // exercise
            p[2] = 1; // pdu type: Entity State
            p[3] = 1; // family
            BinaryPrimitives.WriteUInt32BigEndian(p[4..], (uint)i);
            BinaryPrimitives.WriteUInt16BigEndian(p[8..], Espdu.Size);
            BinaryPrimitives.WriteUInt16BigEndian(p[12..], 1);
            BinaryPrimitives.WriteUInt16BigEndian(p[14..], 2);
            BinaryPrimitives.WriteUInt16BigEndian(p[16..], (ushort)i);
            p[18] = 1;
            p[19] = 0;
            p[20] = 1;
            p[21] = 1;
            BinaryPrimitives.WriteUInt16BigEndian(p[22..], 225);
            for (int k = 0; k < 3; k++)
            {
                BinaryPrimitives.WriteSingleBigEndian(p[(36 + 4 * k)..], 10.0f + k);
                BinaryPrimitives.WriteDoubleBigEndian(p[(48 + 8 * k)..], 1.0e6 + (i + k));
                BinaryPrimitives.WriteSingleBigEndian(p[(72 + 4 * k)..], 0.1f * (k + 1));
                BinaryPrimitives.WriteSingleBigEndian(p[(104 + 4 * k)..], 0.5f);
                BinaryPrimitives.WriteSingleBigEndian(p[(116 + 4 * k)..], 0.01f);
            }
            BinaryPrimitives.WriteUInt32BigEndian(p[84..], 0);
            p[88] = 8;
            p[128] = 1;
            for (int k = 0; k < 11; k++) p[129 + k] = (byte)('A' + k % 26);
            BinaryPrimitives.WriteUInt32BigEndian(p[140..], 0);
        }
        return buf;
    }

    public static DrInputs BuildDrInputs()
    {
        var d = new DrInputs();
        for (int i = 0; i < Count; i++)
        {
            double fi = i;
            for (int k = 0; k < 3; k++)
            {
                d.P0[i][k] = 1.0e6 + fi + k;
                d.V0[i][k] = 10.0 + 0.1 * k;
                d.A0[i][k] = 0.5 + 0.01 * k;
                d.W[i][k] = 0.01 * (k + 1) + 1.0e-4 * (fi % 7.0);
                d.Eul[i][k] = 0.1 * (k + 1);
            }
        }
        return d;
    }

    public static ulong Fnv1a64(ReadOnlySpan<byte> b)
    {
        ulong h = 0xcbf29ce484222325;
        foreach (byte x in b)
        {
            h ^= x;
            h *= 0x100000001b3;
        }
        return h;
    }
}
