// Dependency-free harness, C# side. Parallel in structure to cpp/bench.cpp and rust/src/main.rs.
//
//   Dis.Bench.Aot run [--toolchain LABEL] [--round N] [--reps 25] [--warmup-ms 1000]
//   Dis.Bench.Aot dump
using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using Dis.Kernels;

static class Program
{
    // Barriers. C# has no black_box, so:
    //  * s_len is a mutable static: the JIT cannot treat it as the constant 144, so bounds checks
    //    stay real (the analogue of laundering the length in C++ / black_box(slice) in Rust).
    //  * every decoded struct is stored to s_sink, which escapes, so no field decode is dead.
    private static int s_len = Espdu.Size;
    private static EntityState s_sink;
    private static double s_acc;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int DecodeBatch<L>(byte[] corpus) where L : struct, ILoad
    {
        int len = s_len;
        int decoded = 0;
        double local = 0.0;
        var es = new EntityState();
        for (int i = 0; i < Corpus.Count; i++)
        {
            if (Espdu.Decode<L>(corpus.AsSpan(i * Espdu.Size, len), ref es))
            {
                s_sink = es;
                local += es.Location[0] + es.Entity;
                decoded++;
            }
        }
        s_acc += local;
        return decoded;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int DrBatch(DrInputs d)
    {
        double local = 0.0;
        for (int i = 0; i < Corpus.Count; i++)
        {
            DeadReckoning.DrmRvb(in d.P0[i], in d.V0[i], in d.A0[i], in d.W[i],
                                 d.Eul[i][0], d.Eul[i][1], d.Eul[i][2], DeadReckoning.Dt, out D3 o);
            local += o[0] + o[1] + o[2];
        }
        s_acc += local;
        return Corpus.Count;
    }

    private sealed record Options(string Toolchain, int Round, int Reps, int WarmupMs);

    private static void Measure(Options o, TextWriter w, string kernel, string variant, Func<int> batch)
    {
        // Warmup: at least 5 batches AND at least warmup_ms, identical rule in every language.
        // On CoreCLR this is also what lets tiered compilation reach tier-1 (via OSR) first.
        var warm = Stopwatch.StartNew();
        for (int n = 0; n < 5 || warm.ElapsedMilliseconds < o.WarmupMs; n++) batch();
        for (int rep = 0; rep < o.Reps; rep++)
        {
            long t0 = Stopwatch.GetTimestamp();
            int done = batch();
            double ns = Stopwatch.GetElapsedTime(t0).Ticks * 100.0;
            if (done != Corpus.Count)
            {
                Console.Error.WriteLine($"self-check failed: {kernel}/{variant} processed {done} of {Corpus.Count}");
                Environment.Exit(2);
            }
            w.Write(string.Create(CultureInfo.InvariantCulture,
                $"simple,csharp,{o.Toolchain},{kernel},{variant},{o.Round},{rep},{ns / Corpus.Count:F4}\n"));
        }
    }

    // Under NativeAOT there is no JIT, so dynamic code is unsupported. (This is only a reliable
    // signal because the JIT build does not set PublishAot; see the .csproj.)
    private static bool IsNativeAot => !RuntimeFeature.IsDynamicCodeSupported;

    private static string DefaultToolchain() => $"dotnet-{Environment.Version}-{(IsNativeAot ? "aot" : "jit")}";

    private static int Usage()
    {
        Console.Error.WriteLine("usage: Dis.Bench.Aot run [--toolchain L] [--round N] [--reps N] [--warmup-ms N]\n       Dis.Bench.Aot dump");
        return 64;
    }

    private static int Main(string[] args)
    {
        if (args.Length < 1) return Usage();
        var stdout = new StreamWriter(Console.OpenStandardOutput()) { NewLine = "\n" };
        if (args[0] == "dump")
        {
            Dump.Write(stdout);
            stdout.Flush();
            return 0;
        }
        if (args[0] != "run") return Usage();

        var o = new Options(DefaultToolchain(), 0, 25, 1000);
        for (int i = 1; i < args.Length; i += 2)
        {
            if (i + 1 >= args.Length) return Usage();
            string v = args[i + 1];
            switch (args[i])
            {
                case "--toolchain": o = o with { Toolchain = v }; break;
                case "--round": o = o with { Round = int.Parse(v, CultureInfo.InvariantCulture) }; break;
                case "--reps": o = o with { Reps = int.Parse(v, CultureInfo.InvariantCulture) }; break;
                case "--warmup-ms": o = o with { WarmupMs = int.Parse(v, CultureInfo.InvariantCulture) }; break;
                default: return Usage();
            }
        }

        byte[] corpus = Corpus.Build();
        DrInputs dr = Corpus.BuildDrInputs();
        Measure(o, stdout, "decode", Idiomatic.Name, () => DecodeBatch<Idiomatic>(corpus));
        Measure(o, stdout, "decode", Shift.Name, () => DecodeBatch<Shift>(corpus));
        Measure(o, stdout, "dr", "default", () => DrBatch(dr));
        stdout.Flush();
        Console.Error.WriteLine($"checksum {s_acc:E6} sink {s_sink.Entity}");
        return 0;
    }
}
