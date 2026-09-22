using System.Runtime.CompilerServices;

namespace Dis.Kernels;

// Fixed-size inline arrays: value types, no heap allocation, indexable like C/Rust arrays.
[InlineArray(3)] public struct F32x3 { private float _e; }
[InlineArray(3)] public struct F64x3 { private double _e; }
[InlineArray(11)] public struct Marking11 { private byte _e; }
[InlineArray(9)] public struct Mat3 { private double _e; }

/// <summary>A 3-vector of doubles for dead-reckoning inputs and outputs.</summary>
[InlineArray(3)]
public struct D3
{
    private double _e;

    public D3(double x, double y, double z) { this[0] = x; this[1] = y; this[2] = z; }
}

/// <summary>Decoded ESPDU. A struct, not a class: a class would make this an allocation benchmark.</summary>
public struct EntityState : IEquatable<EntityState>
{
    public ushort Site, Application, Entity;
    public byte ForceId;
    public byte Kind, Domain;
    public ushort Country;
    public F64x3 Location;
    public F32x3 Orientation;
    public F32x3 Velocity;
    public byte DrAlgorithm;
    public F32x3 LinAccel;
    public F32x3 AngVel;
    public uint Appearance;
    public uint Capabilities;
    public Marking11 Marking;

    public readonly bool Equals(EntityState o)
    {
        EntityState a = this, b = o;
        return Site == o.Site && Application == o.Application && Entity == o.Entity &&
               ForceId == o.ForceId && Kind == o.Kind && Domain == o.Domain && Country == o.Country &&
               ((ReadOnlySpan<double>)a.Location).SequenceEqual(b.Location) &&
               ((ReadOnlySpan<float>)a.Orientation).SequenceEqual(b.Orientation) &&
               ((ReadOnlySpan<float>)a.Velocity).SequenceEqual(b.Velocity) &&
               DrAlgorithm == o.DrAlgorithm &&
               ((ReadOnlySpan<float>)a.LinAccel).SequenceEqual(b.LinAccel) &&
               ((ReadOnlySpan<float>)a.AngVel).SequenceEqual(b.AngVel) &&
               Appearance == o.Appearance && Capabilities == o.Capabilities &&
               ((ReadOnlySpan<byte>)a.Marking).SequenceEqual(b.Marking);
    }

    public override readonly bool Equals(object? obj) => obj is EntityState o && Equals(o);
    public override readonly int GetHashCode() => HashCode.Combine(Site, Application, Entity);
}
