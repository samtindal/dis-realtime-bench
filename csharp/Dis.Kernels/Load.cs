using System.Buffers.Binary;
using System.Runtime.CompilerServices;

namespace Dis.Kernels;

/// <summary>Big-endian primitive loads. The caller has already bounds-checked the span.</summary>
/// <remarks>
/// Static abstract members + a struct type argument: the JIT (and NativeAOT) compiles a separate,
/// fully specialized copy of every generic method per idiom, so the choice costs nothing at run time.
/// </remarks>
public interface ILoad
{
    static abstract string Name { get; }
    static abstract ushort U16(ReadOnlySpan<byte> b);
    static abstract uint U32(ReadOnlySpan<byte> b);
    static abstract ulong U64(ReadOnlySpan<byte> b);
}

/// <summary>BinaryPrimitives lowers to a load + REV: the analogue of Rust's from_be_bytes.</summary>
public readonly struct Idiomatic : ILoad
{
    public static string Name => "idiomatic";
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static ushort U16(ReadOnlySpan<byte> b) => BinaryPrimitives.ReadUInt16BigEndian(b);
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static uint U32(ReadOnlySpan<byte> b) => BinaryPrimitives.ReadUInt32BigEndian(b);
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static ulong U64(ReadOnlySpan<byte> b) => BinaryPrimitives.ReadUInt64BigEndian(b);
}

/// <summary>Hand-rolled shifts, transliterated from the C++ LoadShift.</summary>
public readonly struct Shift : ILoad
{
    public static string Name => "shift";
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static ushort U16(ReadOnlySpan<byte> b) => (ushort)((b[0] << 8) | b[1]);
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static uint U32(ReadOnlySpan<byte> b) =>
        ((uint)b[0] << 24) | ((uint)b[1] << 16) | ((uint)b[2] << 8) | b[3];
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static ulong U64(ReadOnlySpan<byte> b)
    {
        ulong v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | b[i];
        return v;
    }
}

/// <summary>
/// Bounds-checked big-endian cursor. A ref struct so it can hold a span and can never escape
/// to the heap. Malformed input returns false; it never throws.
/// </summary>
public ref struct Reader<L> where L : struct, ILoad
{
    private readonly ReadOnlySpan<byte> _buf;
    private int _off;

    public Reader(ReadOnlySpan<byte> buf) { _buf = buf; _off = 0; }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private bool Need(int n) => (uint)n <= (uint)(_buf.Length - _off);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool U8(out byte v)
    {
        if (!Need(1)) { v = 0; return false; }
        v = _buf[_off];
        _off += 1;
        return true;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool U16(out ushort v)
    {
        if (!Need(2)) { v = 0; return false; }
        v = L.U16(_buf.Slice(_off, 2));
        _off += 2;
        return true;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool U32(out uint v)
    {
        if (!Need(4)) { v = 0; return false; }
        v = L.U32(_buf.Slice(_off, 4));
        _off += 4;
        return true;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool U64(out ulong v)
    {
        if (!Need(8)) { v = 0; return false; }
        v = L.U64(_buf.Slice(_off, 8));
        _off += 8;
        return true;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool F32(out float v)
    {
        bool ok = U32(out uint u);
        v = BitConverter.UInt32BitsToSingle(u);
        return ok;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool F64(out double v)
    {
        bool ok = U64(out ulong u);
        v = BitConverter.UInt64BitsToDouble(u);
        return ok;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Skip(int n)
    {
        if (!Need(n)) return false;
        _off += n;
        return true;
    }
}
