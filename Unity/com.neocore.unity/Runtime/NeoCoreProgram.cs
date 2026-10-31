using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace NeoCore.Unity
{
    public enum ExecutionStatus { Completed = 0, Suspended = 1, Failed = 2 }
    public enum ScalarKind { Void = 0, Int = 1, Float = 2, Bool = 3, String = 4, Null = 5,
        Vector2 = 6, Vector3 = 7, Vector4 = 8, Quaternion = 9, Object = 10,
        List = 11, Map = 12, Set = 13, Array = 14, Function = 15, Struct = 16 }

    public partial struct Scalar : IDisposable
    {
        public ScalarKind Kind { get; private set; }
        private long integer;
        private double number;
        private string text;
        private float[] vector;
        private ulong objectId;
        private ValueHandle owned;

        public static Scalar Int(long value) { return new Scalar { Kind = ScalarKind.Int, integer = value }; }
        public static Scalar Float(double value) { return new Scalar { Kind = ScalarKind.Float, number = value }; }
        public static Scalar Bool(bool value) { return new Scalar { Kind = ScalarKind.Bool, integer = value ? 1 : 0 }; }
        public static Scalar String(string value) { if (value == null) throw new ArgumentNullException(nameof(value)); return new Scalar { Kind = ScalarKind.String, text = value }; }
        public static Scalar Null() { return new Scalar { Kind = ScalarKind.Null }; }

        private void Require(ScalarKind expected)
        {
            if (Kind != expected) throw new InvalidOperationException("Expected " + expected + ", received " + Kind);
        }
        public long AsInt() { Require(ScalarKind.Int); return integer; }
        public double AsFloat() { Require(ScalarKind.Float); return number; }
        public bool AsBool() { Require(ScalarKind.Bool); return integer != 0; }
        public string AsString() { Require(ScalarKind.String); return text; }

        internal void Append(IntPtr arguments)
        {
            int success;
            switch (Kind)
            {
                case ScalarKind.Int: success = Native.NcArgumentsInt(arguments, integer); break;
                case ScalarKind.Float: success = Native.NcArgumentsFloat(arguments, number); break;
                case ScalarKind.Bool: success = Native.NcArgumentsBool(arguments, integer != 0 ? 1 : 0); break;
                case ScalarKind.String:
                    byte[] bytes = Encoding.UTF8.GetBytes(text);
                    success = Native.NcArgumentsString(arguments, bytes, checked((uint)bytes.Length));
                    break;
                case ScalarKind.Null: success = Native.NcArgumentsNull(arguments); break;
                case ScalarKind.Vector2:
                case ScalarKind.Vector3:
                case ScalarKind.Vector4:
                case ScalarKind.Quaternion: success = Native.NcArgumentsVector(arguments, (int)Kind, vector); break;
                case ScalarKind.Object:
                    byte[] typeName = Encoding.UTF8.GetBytes(text);
                    success = Native.NcArgumentsObject(arguments, typeName, checked((uint)typeName.Length), objectId);
                    break;
                case ScalarKind.List:
                case ScalarKind.Map:
                case ScalarKind.Set:
                case ScalarKind.Array:
                case ScalarKind.Function:
                case ScalarKind.Struct: success = Native.NcArgumentsValue(arguments, Handle); break;
                default: throw new ArgumentException("Void is not a script argument");
            }
            if (success == 0) throw new InvalidOperationException("Cannot copy a NeoCore argument");
        }
    }

    public sealed class Result
    {
        public ExecutionStatus Status { get; private set; }
        public Scalar Value { get; private set; }
        public string Error { get; private set; }
        public string SourceName { get; private set; }
        public int Line { get; private set; }
        public int Column { get; private set; }

        public Scalar RequireCompleted()
        {
            if (Status == ExecutionStatus.Failed)
                throw new InvalidOperationException(SourceName + ":" + Line + ":" + Column + ": " + Error);
            if (Status != ExecutionStatus.Completed)
                throw new InvalidOperationException("NeoCore execution is suspended; resume it before reading a result");
            return Value;
        }

        internal static string ReadUtf8(IntPtr pointer, uint length)
        {
            if (length == 0) return string.Empty;
            byte[] bytes = new byte[checked((int)length)];
            Marshal.Copy(pointer, bytes, 0, bytes.Length);
            return Encoding.UTF8.GetString(bytes);
        }

        internal static Result Take(IntPtr handle)
        {
            if (handle == IntPtr.Zero) throw new InvalidOperationException("NeoCore could not allocate an execution result");
            try
            {
                var result = new Result { Status = (ExecutionStatus)Native.NcResultStatus(handle) };
                uint length;
                IntPtr pointer = Native.NcResultError(handle, out length);
                result.Error = ReadUtf8(pointer, length);
                pointer = Native.NcResultSource(handle, out length);
                result.SourceName = ReadUtf8(pointer, length);
                result.Line = Native.NcResultLine(handle);
                result.Column = Native.NcResultColumn(handle);
                switch ((ScalarKind)Native.NcResultKind(handle))
                {
                    case ScalarKind.Void: result.Value = default(Scalar); break;
                    case ScalarKind.Int: result.Value = Scalar.Int(Native.NcResultInt(handle)); break;
                    case ScalarKind.Float: result.Value = Scalar.Float(Native.NcResultFloat(handle)); break;
                    case ScalarKind.Bool: result.Value = Scalar.Bool(Native.NcResultBool(handle) != 0); break;
                    case ScalarKind.String:
                        pointer = Native.NcResultString(handle, out length);
                        result.Value = Scalar.String(ReadUtf8(pointer, length));
                        break;
                    case ScalarKind.Null: result.Value = Scalar.Null(); break;
                    case ScalarKind.Vector2:
                    case ScalarKind.Vector3:
                    case ScalarKind.Vector4:
                    case ScalarKind.Quaternion:
                    case ScalarKind.Object:
                    case ScalarKind.List:
                    case ScalarKind.Map:
                    case ScalarKind.Set:
                    case ScalarKind.Array:
                    case ScalarKind.Function:
                    case ScalarKind.Struct: result.Value = Scalar.Take(Native.NcResultValue(handle)); break;
                    default: throw new InvalidOperationException("Unknown NeoCore scalar kind");
                }
                return result;
            }
            finally { Native.NcResultDestroy(handle); }
        }
    }

    public static class Resources
    {
        private static readonly List<IDisposable> live = new List<IDisposable>();
        private static readonly object sync=new object();
        private static readonly List<ValueRelease> deferred=new List<ValueRelease>();
        internal static void Add(IDisposable value) { Drain(); lock(sync) live.Add(value); }
        internal static void Remove(IDisposable value) { lock(sync) live.Remove(value); }
        internal static void Defer(ValueRelease value) { lock(sync) deferred.Add(value); }
        public static void Drain()
        {
            int thread=System.Threading.Thread.CurrentThread.ManagedThreadId;
            List<ValueRelease> pending=null;
            lock(sync)
            {
                for(int i=deferred.Count-1;i>=0;--i)
                    if(deferred[i].ThreadId==thread)
                    {
                        if(pending==null) pending=new List<ValueRelease>();
                        pending.Add(deferred[i]); deferred.RemoveAt(i);
                    }
            }
            if(pending!=null) foreach(var value in pending) value.Dispose();
        }
        public static void DisposeAll()
        {
            Drain();
            while(true)
            {
                IDisposable value;
                lock(sync) { if(live.Count==0) break; value=live[live.Count-1]; }
                value.Dispose();
            }
            Drain();
        }
        public static long AllocationStat(int index)
        { if(index<0 || index>12) throw new ArgumentOutOfRangeException(nameof(index)); Drain(); return Native.NcAllocationStat(index); }
    }

    public sealed partial class Program : IDisposable
    {
        private IntPtr handle;
        private Program(IntPtr value) { handle = value; Resources.Add(this); }

        public static Program Compile(string source, string sourceName = "source.nc", bool debug = true, CompileOptions options = null)
        {
            Resources.Drain();
            Native.CheckVersion();
            if (source == null) throw new ArgumentNullException(nameof(source));
            if (sourceName == null) throw new ArgumentNullException(nameof(sourceName));
            byte[] bytes = Encoding.UTF8.GetBytes(source);
            byte[] name = Encoding.UTF8.GetBytes(sourceName);
            IntPtr result;
            IntPtr program = Native.NcCompileWithOptions(bytes, checked((uint)bytes.Length), name, checked((uint)name.Length),
                debug ? 1 : 0, options == null ? IntPtr.Zero : options.Handle, out result);
            try
            {
                Result.Take(result).RequireCompleted();
                if (program == IntPtr.Zero) throw new InvalidOperationException("NeoCore did not create a program");
                return new Program(program);
            }
            catch { Native.NcProgramDestroy(program); throw; }
        }

        public Instance CreateInstance()
        {
            RequireAlive();
            IntPtr result;
            IntPtr instance = Native.NcInstanceCreate(handle, out result);
            try
            {
                Result.Take(result).RequireCompleted();
                if (instance == IntPtr.Zero) throw new InvalidOperationException("NeoCore did not create an instance");
                return new Instance(instance);
            }
            catch { Native.NcInstanceDestroy(instance); throw; }
        }

        public void Dispose()
        {
            if (handle == IntPtr.Zero) return;
            Native.NcProgramDestroy(handle);
            handle = IntPtr.Zero;
            Resources.Remove(this);
        }
    }

    public sealed partial class Instance : IDisposable
    {
        private IntPtr handle;
        internal Instance(IntPtr value) { handle = value; Resources.Add(this); }
        public bool IsAlive { get { return handle != IntPtr.Zero; } }
        private void RequireAlive()
        {
            Resources.Drain();
            if (!IsAlive) throw new ObjectDisposedException(nameof(Instance));
        }

        public Result Run() { RequireAlive(); return Result.Take(Native.NcRun(handle)); }
        public Result Resume(double deltaTime = 0, uint instructionBudget = 0) { RequireAlive(); return Result.Take(Native.NcResumeBudget(handle, deltaTime, instructionBudget)); }
        public Result Call(string name, params Scalar[] values)
        {
            RequireAlive();
            if (name == null) throw new ArgumentNullException(nameof(name));
            if (values == null) throw new ArgumentNullException(nameof(values));
            byte[] bytes = Encoding.UTF8.GetBytes(name);
            IntPtr arguments = IntPtr.Zero;
            try
            {
                if (values.Length != 0)
                {
                    arguments = Native.NcArgumentsCreate();
                    if (arguments == IntPtr.Zero) throw new InvalidOperationException("Cannot allocate NeoCore arguments");
                    foreach (Scalar value in values) value.Append(arguments);
                }
                return Result.Take(Native.NcCall(handle, bytes, checked((uint)bytes.Length), arguments));
            }
            finally { Native.NcArgumentsDestroy(arguments); }
        }

        public void Dispose()
        {
            if (handle == IntPtr.Zero) return;
            if (Native.NcInstanceTryDestroy(handle) == 0)
                throw new InvalidOperationException("Cannot dispose an instance while a script or host callback is running");
            handle = IntPtr.Zero;
            Resources.Remove(this);
        }
    }
}
