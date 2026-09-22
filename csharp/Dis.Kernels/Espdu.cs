using System.Runtime.CompilerServices;

namespace Dis.Kernels;

public static class Espdu
{
    public const int Size = 144;

    /// <summary>Decodes one Entity State PDU. Returns false on malformed input; never throws.</summary>
    /// <remarks>
    /// Inlining boundary: explicit and identical in every language and harness (see
    /// cpp/include/kernels.hpp). The unit measured is one call, as in real per-datagram use.
    /// </remarks>
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static bool Decode<L>(ReadOnlySpan<byte> buf, ref EntityState o) where L : struct, ILoad
    {
        var r = new Reader<L>(buf);
        if (!r.U8(out _) || !r.U8(out _) || !r.U8(out byte pduType) || !r.U8(out _)) return false;
        if (!r.U32(out _) || !r.U16(out ushort pduLength)) return false;
        if (!r.U8(out _) || !r.U8(out _)) return false;
        if (pduType != 1) return false;
        if (pduLength < Size) return false;

        if (!r.U16(out o.Site) || !r.U16(out o.Application) || !r.U16(out o.Entity)) return false;
        if (!r.U8(out o.ForceId) || !r.U8(out _)) return false;

        if (!r.U8(out o.Kind) || !r.U8(out o.Domain) || !r.U16(out o.Country)) return false;
        if (!r.U8(out _) || !r.U8(out _) || !r.U8(out _) || !r.U8(out _)) return false;
        if (!r.Skip(8)) return false; // alternative entity type

        for (int i = 0; i < 3; i++)
            if (!r.F32(out o.Velocity[i])) return false;
        for (int i = 0; i < 3; i++)
            if (!r.F64(out o.Location[i])) return false;
        for (int i = 0; i < 3; i++)
            if (!r.F32(out o.Orientation[i])) return false;
        if (!r.U32(out o.Appearance)) return false;

        if (!r.U8(out o.DrAlgorithm)) return false;
        if (!r.Skip(15)) return false; // other DR parameters
        for (int i = 0; i < 3; i++)
            if (!r.F32(out o.LinAccel[i])) return false;
        for (int i = 0; i < 3; i++)
            if (!r.F32(out o.AngVel[i])) return false;

        if (!r.U8(out _)) return false; // character set
        for (int i = 0; i < 11; i++)
            if (!r.U8(out o.Marking[i])) return false;
        if (!r.U32(out o.Capabilities)) return false;
        return true;
    }
}
