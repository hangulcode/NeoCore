using System;
using System.Runtime.InteropServices;
using System.Text;

namespace NeoCore.Unity
{
    internal sealed class ValueRelease : IDisposable
    {
        internal IntPtr Pointer;
        internal readonly int ThreadId=System.Threading.Thread.CurrentThread.ManagedThreadId;
        internal ValueRelease(IntPtr value)
        {
            Pointer = value;
            Resources.Add(this);
        }
        public void Dispose()
        {
            if (Pointer == IntPtr.Zero) return;
            if(System.Threading.Thread.CurrentThread.ManagedThreadId!=ThreadId)
                throw new InvalidOperationException("NeoCore values must be released on their owning thread");
            Native.NcValueDestroy(Pointer);
            Pointer = IntPtr.Zero;
            Resources.Remove(this);
        }
    }
    internal sealed class ValueHandle : IDisposable
    {
        private readonly ValueRelease release;
        internal IntPtr Pointer
        {
            get
            {
                if(System.Threading.Thread.CurrentThread.ManagedThreadId!=release.ThreadId)
                    throw new InvalidOperationException("NeoCore values must be used on their owning thread");
                Resources.Drain();
                return release.Pointer;
            }
        }
        internal ValueHandle(IntPtr value)
        {
            if(value==IntPtr.Zero) throw new InvalidOperationException("Cannot allocate a NeoCore value handle");
            release=new ValueRelease(value);
        }
        ~ValueHandle() { if(release!=null) Resources.Defer(release); }
        public void Dispose() { release.Dispose(); GC.SuppressFinalize(this); }
    }

    internal sealed class ValueLease : IDisposable
    {
        internal IntPtr Pointer { get; private set; }
        internal ValueLease(Scalar value)
        {
            IntPtr arguments = Native.NcArgumentsCreate();
            if (arguments == IntPtr.Zero) throw new InvalidOperationException("Cannot allocate arguments");
            try { value.Append(arguments); Pointer = Native.NcArgumentsValueAt(arguments, 0); }
            finally { Native.NcArgumentsDestroy(arguments); }
            if (Pointer == IntPtr.Zero) throw new InvalidOperationException("Cannot copy value");
        }
        public void Dispose() { Native.NcValueDestroy(Pointer); Pointer = IntPtr.Zero; }
    }

    internal sealed class Arguments : IDisposable
    {
        internal IntPtr Pointer { get; private set; }
        internal Arguments(Scalar[] values)
        {
            if (values == null) throw new ArgumentNullException(nameof(values));
            Pointer = Native.NcArgumentsCreate();
            if (Pointer == IntPtr.Zero) throw new InvalidOperationException("Cannot allocate arguments");
            try { foreach (Scalar value in values) value.Append(Pointer); }
            catch { Dispose(); throw; }
        }
        public void Dispose() { Native.NcArgumentsDestroy(Pointer); Pointer = IntPtr.Zero; }
    }

    public partial struct Scalar
    {
        internal IntPtr Handle
        {
            get
            {
                if (owned == null || owned.Pointer == IntPtr.Zero) throw new ObjectDisposedException("NeoCore value");
                return owned.Pointer;
            }
        }
        public static Scalar Vector2(float x, float y) { return new Scalar { Kind = ScalarKind.Vector2, vector = new[] { x, y } }; }
        public static Scalar Vector3(float x, float y, float z) { return new Scalar { Kind = ScalarKind.Vector3, vector = new[] { x, y, z } }; }
        public static Scalar Vector4(float x, float y, float z, float w) { return new Scalar { Kind = ScalarKind.Vector4, vector = new[] { x, y, z, w } }; }
        public static Scalar Quaternion(float w, float x, float y, float z) { return new Scalar { Kind = ScalarKind.Quaternion, vector = new[] { w, x, y, z } }; }
        public static Scalar From(UnityEngine.Vector2 value) { return Vector2(value.x, value.y); }
        public static Scalar From(UnityEngine.Vector3 value) { return Vector3(value.x, value.y, value.z); }
        public static Scalar From(UnityEngine.Vector4 value) { return Vector4(value.x, value.y, value.z, value.w); }
        public static Scalar From(UnityEngine.Quaternion value) { return Quaternion(value.w, value.x, value.y, value.z); }
        public UnityEngine.Vector2 AsVector2() { Require(ScalarKind.Vector2); return new UnityEngine.Vector2(vector[0], vector[1]); }
        public UnityEngine.Vector3 AsVector3() { Require(ScalarKind.Vector3); return new UnityEngine.Vector3(vector[0], vector[1], vector[2]); }
        public UnityEngine.Vector4 AsVector4() { Require(ScalarKind.Vector4); return new UnityEngine.Vector4(vector[0], vector[1], vector[2], vector[3]); }
        public UnityEngine.Quaternion AsQuaternion() { Require(ScalarKind.Quaternion); return new UnityEngine.Quaternion(vector[1], vector[2], vector[3], vector[0]); }
        public static Scalar Object(string typeName, ulong id)
        {
            if (string.IsNullOrEmpty(typeName) || id == 0) throw new ArgumentException("Native values require a type name and nonzero ID");
            return new Scalar { Kind = ScalarKind.Object, text = typeName, objectId = id };
        }
        public ulong AsObject() { Require(ScalarKind.Object); return objectId; }
        public string ObjectType { get { Require(ScalarKind.Object); return text; } }
        public ulong Count { get { return Native.NcValueCount(Handle); } }
        public string TypeName
        {
            get
            {
                using (var value = new ValueLease(this))
                {
                    uint length;
                    IntPtr name = Native.NcValueType(value.Pointer, out length);
                    return Result.ReadUtf8(name, length);
                }
            }
        }
        public Scalar Clone() { return owned == null ? this : Take(Native.NcValueClone(Handle)); }
        public bool SameStorage(Scalar other) { return owned != null && other.owned != null && Native.NcValueSameStorage(Handle, other.Handle) != 0; }
        public Scalar At(ulong index) { return Result.Take(Native.NcValueAt(Handle, index)).RequireCompleted(); }
        public Scalar KeyAt(ulong index) { return Result.Take(Native.NcValueKeyAt(Handle, index)).RequireCompleted(); }
        public Scalar Get(Scalar key)
        { using (var k = new ValueLease(key)) return Result.Take(Native.NcValueGet(Handle, k.Pointer)).RequireCompleted(); }
        public bool Contains(Scalar key)
        { using (var k = new ValueLease(key)) return Result.Take(Native.NcValueContains(Handle, k.Pointer)).RequireCompleted().AsBool(); }
        public void Set(ulong index, Scalar value)
        { using (var v = new ValueLease(value)) Result.Take(Native.NcValueSet(Handle, index, v.Pointer)).RequireCompleted(); }
        public void Put(Scalar key, Scalar value)
        { using (var k = new ValueLease(key)) using (var v = new ValueLease(value)) Result.Take(Native.NcValuePut(Handle, k.Pointer, v.Pointer)).RequireCompleted(); }
        public void Append(Scalar value)
        { using (var v = new ValueLease(value)) Result.Take(Native.NcValueAppend(Handle, v.Pointer)).RequireCompleted(); }
        public bool Remove(Scalar key)
        { using (var k = new ValueLease(key)) return Result.Take(Native.NcValueRemove(Handle, k.Pointer)).RequireCompleted().AsBool(); }
        public void Resize(ulong count) { Result.Take(Native.NcValueResize(Handle, count)).RequireCompleted(); }
        public void Reserve(ulong capacity) { Result.Take(Native.NcValueReserve(Handle, capacity)).RequireCompleted(); }
        public void RemoveAt(ulong index) { Result.Take(Native.NcValueRemoveAt(Handle, index)).RequireCompleted(); }
        public void CopyTo(long[] output, ulong offset=0)
        { if(output==null) throw new ArgumentNullException(nameof(output)); Result.Take(Native.NcArrayReadInt64(Handle,offset,output,(ulong)output.LongLength)).RequireCompleted(); }
        public void CopyTo(double[] output, ulong offset=0)
        { if(output==null) throw new ArgumentNullException(nameof(output)); Result.Take(Native.NcArrayReadFloat64(Handle,offset,output,(ulong)output.LongLength)).RequireCompleted(); }
        public void CopyTo(int[] output, ulong offset=0)
        { if(output==null) throw new ArgumentNullException(nameof(output)); Result.Take(Native.NcArrayReadInt32(Handle,offset,output,(ulong)output.LongLength)).RequireCompleted(); }
        public void CopyTo(float[] output, ulong offset=0)
        { if(output==null) throw new ArgumentNullException(nameof(output)); Result.Take(Native.NcArrayReadFloat32(Handle,offset,output,(ulong)output.LongLength)).RequireCompleted(); }
        public void CopyFrom(long[] input, ulong offset=0)
        { if(input==null) throw new ArgumentNullException(nameof(input)); Result.Take(Native.NcArrayWriteInt64(Handle,offset,input,(ulong)input.LongLength)).RequireCompleted(); }
        public void CopyFrom(double[] input, ulong offset=0)
        { if(input==null) throw new ArgumentNullException(nameof(input)); Result.Take(Native.NcArrayWriteFloat64(Handle,offset,input,(ulong)input.LongLength)).RequireCompleted(); }
        public void CopyFrom(int[] input, ulong offset=0)
        { if(input==null) throw new ArgumentNullException(nameof(input)); Result.Take(Native.NcArrayWriteInt32(Handle,offset,input,(ulong)input.LongLength)).RequireCompleted(); }
        public void CopyFrom(float[] input, ulong offset=0)
        { if(input==null) throw new ArgumentNullException(nameof(input)); Result.Take(Native.NcArrayWriteFloat32(Handle,offset,input,(ulong)input.LongLength)).RequireCompleted(); }
        public void Clear() { Result.Take(Native.NcValueClear(Handle)).RequireCompleted(); }
        public void Dispose() { if (owned != null) owned.Dispose(); }

        internal static Scalar Take(IntPtr value)
        {
            if (value == IntPtr.Zero) throw new InvalidOperationException("Cannot capture a NeoCore value");
            bool transferred = false;
            try
            {
                var result = new Scalar { Kind = (ScalarKind)Native.NcValueKind(value) };
                uint length;
                switch (result.Kind)
                {
                    case ScalarKind.Void: return result;
                    case ScalarKind.Int: return Int(Native.NcValueInt(value));
                    case ScalarKind.Float: return Float(Native.NcValueFloat(value));
                    case ScalarKind.Bool: return Bool(Native.NcValueBool(value) != 0);
                    case ScalarKind.String:
                        IntPtr textValue = Native.NcValueString(value, out length);
                        return String(Result.ReadUtf8(textValue, length));
                    case ScalarKind.Null: return Null();
                    case ScalarKind.Vector2:
                    case ScalarKind.Vector3:
                    case ScalarKind.Vector4:
                    case ScalarKind.Quaternion:
                        result.vector = new float[4];
                        if (Native.NcValueVector(value, result.vector, 4) == 0) throw new InvalidOperationException("Invalid native vector result");
                        return result;
                    case ScalarKind.Object:
                        IntPtr type = Native.NcValueType(value, out length);
                        return Object(Result.ReadUtf8(type, length), Native.NcValueObject(value));
                    case ScalarKind.List:
                    case ScalarKind.Map:
                    case ScalarKind.Set:
                    case ScalarKind.Array:
                    case ScalarKind.Function:
                    case ScalarKind.Struct:
                        result.owned = new ValueHandle(value);
                        transferred = true;
                        return result;
                    default: throw new InvalidOperationException("Unknown NeoCore value kind");
                }
            }
            finally { if (!transferred) Native.NcValueDestroy(value); }
        }
    }

    public sealed partial class Program
    {
        private void RequireAlive() { Resources.Drain(); if (handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(Program)); }
        public Scalar CreateValue(string typeName)
        {
            RequireAlive();
            byte[] name = Encoding.UTF8.GetBytes(typeName ?? throw new ArgumentNullException(nameof(typeName)));
            return Result.Take(Native.NcValueCreate(handle, name, checked((uint)name.Length))).RequireCompleted();
        }
        public long TrimMemory(bool force = false) { RequireAlive(); return Native.NcProgramTrimMemory(handle, force ? 1 : 0); }
        public long CollectCycles(bool force = false) { RequireAlive(); return Native.NcProgramCollectCycles(handle, force ? 1 : 0); }
        public Scalar FindFunction(string name)
        {
            RequireAlive();
            byte[] bytes=Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            return Result.Take(Native.NcFindFunction(handle,bytes,checked((uint)bytes.Length))).RequireCompleted();
        }
        public Scalar CreateValue(string typeName, params Scalar[] fields)
        {
            RequireAlive();
            byte[] name=Encoding.UTF8.GetBytes(typeName ?? throw new ArgumentNullException(nameof(typeName)));
            using(var arguments=new Arguments(fields))
                return Result.Take(Native.NcValueCreateFields(handle,name,checked((uint)name.Length),arguments.Pointer)).RequireCompleted();
        }
        public Scalar ParseJson(string typeName, string json, bool strictIntegers=true)
        {
            RequireAlive();
            byte[] type=Encoding.UTF8.GetBytes(typeName ?? throw new ArgumentNullException(nameof(typeName)));
            byte[] text=Encoding.UTF8.GetBytes(json ?? throw new ArgumentNullException(nameof(json)));
            return Result.Take(Native.NcParseJson(handle,type,checked((uint)type.Length),text,checked((uint)text.Length),strictIntegers?1:0)).RequireCompleted();
        }
        public string ToJson(Scalar value, bool sortKeys=false)
        {
            RequireAlive();
            using(var lease=new ValueLease(value)) return Result.Take(Native.NcToJson(handle,lease.Pointer,sortKeys?1:0)).RequireCompleted().AsString();
        }
        public void SetEmptyPageHoldSeconds(float seconds) { RequireAlive(); Native.NcProgramSetEmptyPageHoldSeconds(handle, seconds); }
        public void SetTrimPagesPerCall(int pages) { RequireAlive(); Native.NcProgramSetTrimPagesPerCall(handle, pages); }
        public byte[] SaveImage()
        {
            RequireAlive();
            IntPtr result;
            IntPtr image = Native.NcSaveImage(handle, out result);
            try
            {
                Result.Take(result).RequireCompleted();
                if (image == IntPtr.Zero) throw new InvalidOperationException("Image save failed");
                ulong length;
                IntPtr data = Native.NcBytesData(image, out length);
                byte[] bytes = new byte[checked((int)length)];
                if (bytes.Length != 0) Marshal.Copy(data, bytes, 0, bytes.Length);
                return bytes;
            }
            finally { Native.NcBytesDestroy(image); }
        }
        public static Program LoadImage(byte[] bytes, CompileOptions options = null)
        {
            Resources.Drain();
            Native.CheckVersion();
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));
            IntPtr result;
            IntPtr program = Native.NcLoadImage(bytes, checked((ulong)bytes.LongLength), options == null ? IntPtr.Zero : options.Handle, out result);
            try
            {
                Result.Take(result).RequireCompleted();
                if (program == IntPtr.Zero) throw new InvalidOperationException("Image load failed");
                return new Program(program);
            }
            catch { Native.NcProgramDestroy(program); throw; }
        }
    }

    public sealed partial class Instance
    {
        public bool IsSuspended { get { RequireAlive(); return Native.NcIsSuspended(handle) != 0; } }
        public bool IsRunning { get { RequireAlive(); return Native.NcIsRunning(handle) != 0; } }
        public void Cancel() { RequireAlive(); Result.Take(Native.NcCancel(handle)).RequireCompleted(); }
        public void Reset() { RequireAlive(); Result.Take(Native.NcReset(handle)).RequireCompleted(); }
        public Result CallLimited(string name, int timeoutMs, uint instructionBudget, params Scalar[] values)
        {
            RequireAlive();
            byte[] bytes = Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            using (var arguments = new Arguments(values))
                return Result.Take(Native.NcCallLimited(handle, bytes, checked((uint)bytes.Length), arguments.Pointer, timeoutMs, instructionBudget));
        }
        public Result Call(Scalar function, params Scalar[] values) { return CallLimited(function, -1, 0, values); }
        public Result CallLimited(Scalar function, int timeoutMs, uint instructionBudget, params Scalar[] values)
        {
            RequireAlive();
            if (function.Kind != ScalarKind.Function) throw new ArgumentException("Value is not a function", nameof(function));
            using (var arguments = new Arguments(values))
                return Result.Take(Native.NcCallFunction(handle, function.Handle, arguments.Pointer, timeoutMs, instructionBudget));
        }
        public Scalar ReadGlobal(string name)
        {
            RequireAlive();
            byte[] bytes = Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            return Result.Take(Native.NcReadGlobal(handle, bytes, checked((uint)bytes.Length))).RequireCompleted();
        }
        public void WriteGlobal(string name, Scalar value)
        {
            RequireAlive();
            byte[] bytes = Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            using (var v = new ValueLease(value)) Result.Take(Native.NcWriteGlobal(handle, bytes, checked((uint)bytes.Length), v.Pointer)).RequireCompleted();
        }
        public void SetHostObject(string name, ulong id)
        {
            RequireAlive();
            byte[] bytes = Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            Result.Take(Native.NcInstanceSetHostObject(handle, bytes, checked((uint)bytes.Length), id)).RequireCompleted();
        }
    }
}
