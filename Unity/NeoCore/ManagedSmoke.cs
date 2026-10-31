using System;
using System.IO;
using NeoCore.Unity;

internal static class ManagedSmoke
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static int Main(string[] args)
    {
        try
        {
            string source = File.ReadAllText(args[0]);
            using (var program = NeoCore.Unity.Program.Compile(source, "demo.nc"))
            using (var first = program.CreateInstance())
            using (var second = program.CreateInstance())
            {
                first.Run().RequireCompleted();
                program.Dispose();
                for (int i = 0; i != 16; ++i)
                {
                    double time = i * 0.125;
                    Require(first.Call("Update", Scalar.Float(0.125), Scalar.Float(time)).RequireCompleted().AsBool() == (i == 0), "first tick flag");
                    Require(Math.Abs(first.Call("PositionX").RequireCompleted().AsFloat() - (float)(Math.Sin(time) * 2)) < 0.000001, "managed wobble x");
                    Require(first.Call("PositionY").RequireCompleted().AsFloat() == 1.125, "managed wobble y");
                    Require(Math.Abs(first.Call("PositionZ").RequireCompleted().AsFloat() - (float)(Math.Cos(time) * 2)) < 0.000001, "managed wobble z");
                }
                Require(first.Call("Ticks").RequireCompleted().AsInt() == 16, "first instance ticks");
                Require(second.Call("Ticks").RequireCompleted().AsInt() == 0, "independent instance ticks");
                Require(first.Call("Update", Scalar.Bool(true), Scalar.Float(0)).Status == ExecutionStatus.Failed, "checked argument type");
                Require(first.Call("Ticks").RequireCompleted().AsInt() == 16, "rejected call has no side effects");
            }
            source = "export int I(int x) { return x; }\n"
                + "export float F(float x) { return x; }\n"
                + "export string S(string x) { return x; }\n"
                + "export bool B(bool x) { return x; }\n"
                + "export int? N() { return null; }\n"
                + "export int Wait() { sleep(0); return 42; }\n"
                + "export int Bad() { int zero = 0; return 1 / zero; }\n";
            using (var program = NeoCore.Unity.Program.Compile(source, "boundary.nc"))
            using (var instance = program.CreateInstance())
            {
                Require(instance.Call("I", Scalar.Int(9007199254740993L)).RequireCompleted().AsInt() == 9007199254740993L, "exact int64");
                Require(instance.Call("F", Scalar.Float(1.0000000000000002)).RequireCompleted().AsFloat() == 1.0000000000000002, "binary64 precision");
                Result retained = instance.Call("S", Scalar.String("UTF-8 \u2603"));
                Require(retained.RequireCompleted().AsString() == "UTF-8 \u2603", "UTF-8 string round trip");
                Require(instance.Call("B", Scalar.Bool(true)).RequireCompleted().AsBool(), "bool round trip");
                Require(retained.Value.AsString() == "UTF-8 \u2603", "string survives next call");
                Require(instance.Call("N").RequireCompleted().Kind == ScalarKind.Null, "nullable scalar result");
                Require(instance.Call("Wait").Status == ExecutionStatus.Suspended, "suspended status");
                Require(instance.Resume().RequireCompleted().AsInt() == 42, "resumed scalar result");
                Result error = instance.Call("Bad");
                Require(error.Status == ExecutionStatus.Failed && error.Line == 7 && error.SourceName == "boundary.nc", "runtime source coordinates");
                NeoCore.Unity.Resources.DisposeAll();
                Require(!instance.IsAlive, "resource cleanup invalidates managed handle");
                bool disposed = false;
                try { instance.Run(); } catch (ObjectDisposedException) { disposed = true; }
                Require(disposed, "disposed call rejected before native access");
            }
            bool rejected = false;
            try { using (NeoCore.Unity.Program.Compile("var dynamic = 1;", "reject.nc")) { } }
            catch (InvalidOperationException error) { rejected = error.Message.Contains("reject.nc:1:"); }
            Require(rejected, "typed frontend rejection and diagnostics");
            Console.WriteLine("NeoCore managed smoke passed: demo.nc, scalar precision/UTF-8, instances, rejection, suspension, cleanup");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
        finally { NeoCore.Unity.Resources.DisposeAll(); }
    }
}
