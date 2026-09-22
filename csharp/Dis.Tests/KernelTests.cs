using Dis.Kernels;
using Xunit;

namespace Dis.Tests;

public class KernelTests
{
    static void KnownFields<L>() where L : struct, ILoad
    {
        byte[] c = Corpus.Build();
        var es = new EntityState();
        Assert.True(Espdu.Decode<L>(c.AsSpan(0, Espdu.Size), ref es));
        Assert.Equal((ushort)1, es.Site);
        Assert.Equal((ushort)2, es.Application);
        Assert.Equal((ushort)0, es.Entity);
        Assert.Equal((byte)1, es.ForceId);
        Assert.Equal((ushort)225, es.Country);
        Assert.Equal(10.0f, es.Velocity[0]);
        Assert.Equal(12.0f, es.Velocity[2]);
        Assert.Equal(1.0e6, es.Location[0]);
        Assert.Equal(1.0e6 + 2.0, es.Location[2]);
        Assert.Equal(0.2f, es.Orientation[1]);
        Assert.Equal((byte)8, es.DrAlgorithm);
        Assert.Equal(0.5f, es.LinAccel[1]);
        Assert.Equal(0.01f, es.AngVel[2]);
        Assert.Equal("ABCDEFGHIJK"u8.ToArray(), ((ReadOnlySpan<byte>)es.Marking).ToArray());
    }

    static void EveryPduHasItsIndex<L>() where L : struct, ILoad
    {
        byte[] c = Corpus.Build();
        var es = new EntityState();
        for (int i = 0; i < Corpus.Count; i++)
        {
            Assert.True(Espdu.Decode<L>(c.AsSpan(i * Espdu.Size, Espdu.Size), ref es));
            Assert.Equal((ushort)i, es.Entity);
        }
    }

    static void RejectsMalformed<L>() where L : struct, ILoad
    {
        byte[] c = Corpus.Build();
        var es = new EntityState();
        for (int n = 0; n < Espdu.Size; n++)
            Assert.False(Espdu.Decode<L>(c.AsSpan(0, n), ref es), $"accepted truncated length {n}");
        byte[] badType = c[..Espdu.Size];
        badType[2] = 2;
        Assert.False(Espdu.Decode<L>(badType, ref es));
        byte[] shortLen = c[..Espdu.Size];
        shortLen[9] = 143;
        Assert.False(Espdu.Decode<L>(shortLen, ref es));
    }

    [Fact] public void IdiomaticKnownFields() => KnownFields<Idiomatic>();
    [Fact] public void ShiftKnownFields() => KnownFields<Shift>();
    [Fact] public void IdiomaticEveryPdu() => EveryPduHasItsIndex<Idiomatic>();
    [Fact] public void ShiftEveryPdu() => EveryPduHasItsIndex<Shift>();
    [Fact] public void IdiomaticRejectsMalformed() => RejectsMalformed<Idiomatic>();
    [Fact] public void ShiftRejectsMalformed() => RejectsMalformed<Shift>();

    [Fact]
    public void IdiomsAgree()
    {
        byte[] c = Corpus.Build();
        for (int i = 0; i < Corpus.Count; i++)
        {
            EntityState a = default, b = default;
            Assert.True(Espdu.Decode<Idiomatic>(c.AsSpan(i * Espdu.Size, Espdu.Size), ref a));
            Assert.True(Espdu.Decode<Shift>(c.AsSpan(i * Espdu.Size, Espdu.Size), ref b));
            Assert.Equal(a, b);
        }
    }

    [Fact]
    public void DrZeroOmegaIsKinematic()
    {
        D3 p0 = new(1.0e6, 2.0e6, 3.0e6), v0 = new(1.0, 0.0, 0.0), a0 = default, w = default;
        DeadReckoning.DrmRvb(in p0, in v0, in a0, in w, 0.0, 0.0, 0.0, 2.0, out D3 r);
        Assert.Equal(1.0e6 + 2.0, r[0]);
        Assert.Equal(2.0e6, r[1]);
        Assert.Equal(3.0e6, r[2]);
    }

    [Fact]
    public void CorpusSizeAndHashFunction()
    {
        Assert.Equal(Corpus.Count * Espdu.Size, Corpus.Build().Length);
        Assert.Equal(0xaf63dc4c8601ec8cUL, Corpus.Fnv1a64("a"u8));
    }
}
