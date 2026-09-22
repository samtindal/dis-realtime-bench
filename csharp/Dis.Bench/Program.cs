// BenchmarkDotNet harness, C# side. Same kernels and batch shape as Dis.Bench.Aot: one invocation
// = one 1024-op batch; OperationsPerInvoke makes BDN report per-operation times and allocations.
using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Running;
using Dis.Kernels;

BenchmarkSwitcher.FromAssembly(typeof(Kernels).Assembly).Run(args);

[MemoryDiagnoser]
public class Kernels
{
    private byte[] _corpus = [];
    private DrInputs _dr = null!;
    private int _len = Espdu.Size; // mutable field: the JIT cannot fold it to the constant 144
    private EntityState _sink;      // every decoded struct escapes here, so no field decode is dead

    [GlobalSetup]
    public void Setup()
    {
        _corpus = Corpus.Build();
        _dr = Corpus.BuildDrInputs();
    }

    [Benchmark(OperationsPerInvoke = Corpus.Count)]
    public int DecodeIdiomatic() => Decode<Idiomatic>();

    [Benchmark(OperationsPerInvoke = Corpus.Count)]
    public int DecodeShift() => Decode<Shift>();

    [Benchmark(OperationsPerInvoke = Corpus.Count)]
    public double Dr()
    {
        DrInputs d = _dr;
        double local = 0.0;
        for (int i = 0; i < Corpus.Count; i++)
        {
            DeadReckoning.DrmRvb(in d.P0[i], in d.V0[i], in d.A0[i], in d.W[i],
                                 d.Eul[i][0], d.Eul[i][1], d.Eul[i][2], DeadReckoning.Dt, out D3 o);
            local += o[0] + o[1] + o[2];
        }
        return local;
    }

    private int Decode<L>() where L : struct, ILoad
    {
        int len = _len, decoded = 0;
        var es = new EntityState();
        for (int i = 0; i < Corpus.Count; i++)
        {
            if (Espdu.Decode<L>(_corpus.AsSpan(i * Espdu.Size, len), ref es))
            {
                _sink = es;
                decoded++;
            }
        }
        if (decoded != Corpus.Count) throw new InvalidOperationException("self-check failed");
        return decoded;
    }
}
