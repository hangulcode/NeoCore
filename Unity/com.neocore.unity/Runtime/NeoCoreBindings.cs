using System;
using System.Runtime.InteropServices;
using System.Text;
using AOT;

namespace NeoCore.Unity
{
    public readonly struct ScriptType
    {
        internal readonly uint Id;
        internal readonly CompileOptions Owner;
        internal ScriptType(uint id, CompileOptions owner = null) { Id = id; Owner = owner; }
    }
    public static class ScriptTypes
    {
        public static ScriptType Void { get { return new ScriptType(Native.NcTypeBuiltin((int)ScalarKind.Void)); } }
        public static ScriptType Int { get { return new ScriptType(Native.NcTypeBuiltin((int)ScalarKind.Int)); } }
        public static ScriptType Float { get { return new ScriptType(Native.NcTypeBuiltin((int)ScalarKind.Float)); } }
        public static ScriptType Bool { get { return new ScriptType(Native.NcTypeBuiltin((int)ScalarKind.Bool)); } }
        public static ScriptType String { get { return new ScriptType(Native.NcTypeBuiltin((int)ScalarKind.String)); } }
        public static ScriptType Vector2 { get { return new ScriptType(Native.NcTypeBuiltin((int)ScalarKind.Vector2)); } }
        public static ScriptType Vector3 { get { return new ScriptType(Native.NcTypeBuiltin((int)ScalarKind.Vector3)); } }
        public static ScriptType Vector4 { get { return new ScriptType(Native.NcTypeBuiltin((int)ScalarKind.Vector4)); } }
        public static ScriptType Quaternion { get { return new ScriptType(Native.NcTypeBuiltin((int)ScalarKind.Quaternion)); } }
    }
    public sealed class ModuleSource
    {
        public string Source { get; private set; }
        public string SourceName { get; private set; }
        public ModuleSource(string source, string sourceName)
        { Source = source ?? throw new ArgumentNullException(nameof(source)); SourceName = sourceName ?? throw new ArgumentNullException(nameof(sourceName)); }
    }
    public readonly struct ScriptField
    {
        public readonly string Name;
        public readonly ScriptType Type;
        public ScriptField(string name, ScriptType type) { Name=name ?? throw new ArgumentNullException(nameof(name)); Type=type; }
    }

    public sealed class CompileOptions : IDisposable
    {
        private IntPtr handle;
        internal IntPtr Handle { get { if (handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(CompileOptions)); return handle; } }
        private sealed class HostPair { internal Action<HostCall> First; internal Action<HostCall> Second; }
        private static readonly Native.ReleaseCallback release = Release;
        private static readonly Native.ObjectResolverCallback resolver = Resolve;
        private static readonly Native.HostCallback first = First;
        private static readonly Native.HostCallback second = Second;
        private static readonly Native.ModuleLoaderCallback loader = LoadModule;
        private static readonly Native.HttpTransportCallback transport = SendHttp;

        public CompileOptions()
        {
            Resources.Drain();
            Native.CheckVersion();
            handle = Native.NcOptionsCreate();
            if (handle == IntPtr.Zero) throw new InvalidOperationException("Cannot allocate NeoCore compile options");
            Resources.Add(this);
        }
        private uint TypeId(ScriptType type)
        {
            if (type.Id == 0 || (type.Owner != null && !ReferenceEquals(type.Owner, this)))
                throw new ArgumentException("Script type belongs to a different schema");
            return type.Id;
        }
        private ScriptType CheckedType(uint id)
        { if (id == 0) throw Error(); return new ScriptType(id, this); }
        private Exception Error()
        {
            uint length;
            IntPtr message = Native.NcOptionsError(Handle, out length);
            string text = Result.ReadUtf8(message, length);
            return new InvalidOperationException(string.IsNullOrEmpty(text) ? "Invalid NeoCore binding definition" : text);
        }
        private void Register(object state, Func<IntPtr, bool> action)
        {
            GCHandle root = GCHandle.Alloc(state);
            bool transferred = false;
            try { transferred = action(GCHandle.ToIntPtr(root)); if (!transferred) throw Error(); }
            finally { if (!transferred) root.Free(); }
        }
        public ScriptType Object(string name)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            return CheckedType(Native.NcSchemaObject(Handle, bytes, checked((uint)bytes.Length)));
        }
        private void Define(string name, int kind, string literal)
        {
            byte[] key=Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            byte[] text=Encoding.UTF8.GetBytes(literal ?? throw new ArgumentNullException(nameof(literal)));
            if(Native.NcOptionsDefine(Handle,key,checked((uint)key.Length),kind,text,checked((uint)text.Length))==0) throw Error();
        }
        public void DefineIdentifier(string name,string identifier) { Define(name,0,identifier); }
        public void DefineInt(string name,long value) { Define(name,1,value.ToString(System.Globalization.CultureInfo.InvariantCulture)); }
        public void DefineFloat(string name,double value)
        {
            if(double.IsNaN(value) || double.IsInfinity(value)) throw new ArgumentOutOfRangeException(nameof(value));
            string literal=value==0 && BitConverter.DoubleToInt64Bits(value)<0?"-0.0":value.ToString("R",System.Globalization.CultureInfo.InvariantCulture);
            if(literal.IndexOf('.')<0 && literal.IndexOf('E')<0 && literal.IndexOf('e')<0) literal+=".0";
            Define(name,2,literal);
        }
        public void DefineBool(string name,bool value) { Define(name,4,value?"true":"false"); }
        public void DefineNull(string name) { Define(name,5,"null"); }
        public void DefineString(string name,string value)
        {
            if(value==null) throw new ArgumentNullException(nameof(value));
            if(value.IndexOf('\0')>=0) throw new ArgumentException("Compile-time string literals cannot contain NUL; pass this value as a host string instead.",nameof(value));
            var text=new StringBuilder("\"");
            foreach(char character in value)
            {
                switch(character)
                {
                    case '\t': text.Append("\\t"); break;
                    case '\n': text.Append("\\n"); break;
                    case '\r': text.Append("\\r"); break;
                    case '"': text.Append("\\\""); break;
                    case '\\': text.Append("\\\\"); break;
                    default: text.Append(character); break;
                }
            }
            text.Append('"'); Define(name,3,text.ToString());
        }
        public ScriptType Nullable(ScriptType type) { return CheckedType(Native.NcTypeNullable(Handle, TypeId(type))); }
        public ScriptType Struct(string name)
        {
            byte[] bytes=Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            return CheckedType(Native.NcSchemaStruct(Handle,bytes,checked((uint)bytes.Length)));
        }
        public void DefineFields(ScriptType structure, params ScriptField[] fields)
        {
            if(fields==null) throw new ArgumentNullException(nameof(fields));
            var native=new Native.Field[fields.Length];
            try
            {
                for(int i=0;i<fields.Length;++i)
                {
                    byte[] name=Encoding.UTF8.GetBytes(fields[i].Name ?? throw new ArgumentException("Field name is null"));
                    native[i].Type=TypeId(fields[i].Type);
                    native[i].Length=checked((uint)name.Length);
                    native[i].Name=Marshal.AllocHGlobal(name.Length+1);
                    Marshal.Copy(name,0,native[i].Name,name.Length);
                    Marshal.WriteByte(native[i].Name,name.Length,0);
                }
                if(Native.NcSchemaStructFields(Handle,TypeId(structure),native,checked((uint)native.Length))==0) throw Error();
            }
            finally { foreach(var field in native) if(field.Name!=IntPtr.Zero) Marshal.FreeHGlobal(field.Name); }
        }
        public ScriptType List(ScriptType element) { return CheckedType(Native.NcTypeCollection(Handle, (int)ScalarKind.List, TypeId(element), 0, 0)); }
        public ScriptType Set(ScriptType element) { return CheckedType(Native.NcTypeCollection(Handle, (int)ScalarKind.Set, TypeId(element), 0, 0)); }
        public ScriptType Array(ScriptType element) { return CheckedType(Native.NcTypeCollection(Handle, (int)ScalarKind.Array, TypeId(element), 0, 0)); }
        public ScriptType Array32(ScriptType element) { return CheckedType(Native.NcTypeCollection(Handle, 17, TypeId(element), 0, 0)); }
        public ScriptType Map(ScriptType key, ScriptType value) { return CheckedType(Native.NcTypeCollection(Handle, (int)ScalarKind.Map, 0, TypeId(key), TypeId(value))); }
        public ScriptType Function(ScriptType result, params ScriptType[] parameters)
        {
            if (parameters == null) throw new ArgumentNullException(nameof(parameters));
            uint[] ids = new uint[parameters.Length];
            for (int i = 0; i < ids.Length; ++i) ids[i] = TypeId(parameters[i]);
            return CheckedType(Native.NcTypeFunction(Handle, TypeId(result), ids, checked((uint)ids.Length)));
        }
        public void SetResolver(Func<uint, ulong, bool> resolve)
        {
            if (resolve == null) throw new ArgumentNullException(nameof(resolve));
            Register(resolve, p => Native.NcOptionsSetResolver(Handle, resolver, p, release) != 0);
        }
        public void SetModuleLoader(Func<string, string, ModuleSource> load)
        {
            if (load == null) throw new ArgumentNullException(nameof(load));
            Register(load, p => Native.NcOptionsSetModuleLoader(Handle, loader, p, release) != 0);
        }
        public void SetHttpTransport(Func<HttpRequest, HttpResponse> send)
        {
            if (send == null) throw new ArgumentNullException(nameof(send));
            Register(send, p => Native.NcOptionsSetHttpTransport(Handle, transport, p, release) != 0);
        }
        public void Method(ScriptType owner, string name, ScriptType result, ScriptType[] parameters, Action<HostCall> callback)
        {
            if (callback == null) throw new ArgumentNullException(nameof(callback));
            if (parameters == null) throw new ArgumentNullException(nameof(parameters));
            byte[] bytes = Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            uint ownerId = TypeId(owner), resultId = TypeId(result);
            uint[] ids = new uint[parameters.Length];
            for (int i = 0; i < ids.Length; ++i) ids[i] = TypeId(parameters[i]);
            Register(new HostPair { First = callback }, p => Native.NcSchemaMethod(Handle, ownerId, bytes,
                checked((uint)bytes.Length), resultId, ids, checked((uint)ids.Length), first, p, release) != 0);
        }
        public void Property(ScriptType owner, string name, ScriptType type, Action<HostCall> getter, Action<HostCall> setter = null)
        {
            if (getter == null && setter == null) throw new ArgumentException("Property requires at least one accessor");
            byte[] bytes = Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            uint ownerId = TypeId(owner), valueId = TypeId(type);
            Register(new HostPair { First = getter, Second = setter }, p => Native.NcSchemaProperty(Handle,
                ownerId, bytes, checked((uint)bytes.Length), valueId, getter == null ? null : first,
                setter == null ? null : second, p, release) != 0);
        }
        public void Global(string name, ScriptType type, ulong id)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            if (Native.NcSchemaGlobal(Handle, bytes, checked((uint)bytes.Length), TypeId(type), id) == 0) throw Error();
        }
        public void Dispose()
        {
            if (handle == IntPtr.Zero) return;
            Native.NcOptionsDestroy(handle);
            handle = IntPtr.Zero;
            Resources.Remove(this);
        }
        [MonoPInvokeCallback(typeof(Native.ReleaseCallback))]
        private static void Release(IntPtr pointer) { if (pointer != IntPtr.Zero) GCHandle.FromIntPtr(pointer).Free(); }
        [MonoPInvokeCallback(typeof(Native.ObjectResolverCallback))]
        private static IntPtr Resolve(IntPtr pointer, uint type, ulong id)
        {
            try { return ((Func<uint, ulong, bool>)GCHandle.FromIntPtr(pointer).Target)(type, id) ? new IntPtr(1) : IntPtr.Zero; }
            catch { return IntPtr.Zero; }
        }
        private static int Invoke(IntPtr pointer, IntPtr context, bool isSecond)
        {
            var call = new HostCall(context);
            try
            {
                HostPair pair = (HostPair)GCHandle.FromIntPtr(pointer).Target;
                (isSecond ? pair.Second : pair.First)(call);
                return 1;
            }
            catch (Exception error) { call.Fail(error.Message); return 0; }
            finally { call.Close(); }
        }
        [MonoPInvokeCallback(typeof(Native.HostCallback))]
        private static int First(IntPtr pointer, IntPtr context) { return Invoke(pointer, context, false); }
        [MonoPInvokeCallback(typeof(Native.HostCallback))]
        private static int Second(IntPtr pointer, IntPtr context) { return Invoke(pointer, context, true); }
        [MonoPInvokeCallback(typeof(Native.ModuleLoaderCallback))]
        private static int LoadModule(IntPtr pointer, IntPtr name, uint nameLength, IntPtr importer, uint importerLength, IntPtr output)
        {
            try
            {
                var load = (Func<string, string, ModuleSource>)GCHandle.FromIntPtr(pointer).Target;
                ModuleSource source = load(Result.ReadUtf8(name, nameLength), Result.ReadUtf8(importer, importerLength));
                if (source == null) throw new InvalidOperationException("Module source not found");
                byte[] text = Encoding.UTF8.GetBytes(source.Source), sourceName = Encoding.UTF8.GetBytes(source.SourceName);
                Native.NcModuleSource(output, text, checked((uint)text.Length), sourceName, checked((uint)sourceName.Length));
                return 1;
            }
            catch (Exception error)
            {
                byte[] text = Encoding.UTF8.GetBytes(error.Message);
                Native.NcModuleError(output, text, checked((uint)text.Length));
                return 0;
            }
        }
        [MonoPInvokeCallback(typeof(Native.HttpTransportCallback))]
        private static void SendHttp(IntPtr pointer, IntPtr input, IntPtr output)
        {
            HttpRequest request = null;
            try
            {
                request = new HttpRequest(input);
                var send = (Func<HttpRequest, HttpResponse>)GCHandle.FromIntPtr(pointer).Target;
                HttpResponse response = send(request);
                byte[] body = Encoding.UTF8.GetBytes(response.Body ?? string.Empty);
                byte[] error = Encoding.UTF8.GetBytes(response.Error ?? string.Empty);
                Native.NcHttpComplete(output, response.Success ? 1 : 0, body, checked((uint)body.Length), error, checked((uint)error.Length));
            }
            catch (Exception error)
            {
                byte[] text = Encoding.UTF8.GetBytes(error.Message);
                Native.NcHttpComplete(output, 0, new byte[0], 0, text, checked((uint)text.Length));
            }
            finally { if (request != null) request.Close(); }
        }
    }

    public sealed class HostCall
    {
        private IntPtr handle;
        internal HostCall(IntPtr value) { handle = value; }
        private IntPtr Handle { get { if (handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(HostCall)); return handle; } }
        internal void Close() { handle = IntPtr.Zero; }
        public uint ArgumentCount { get { return Native.NcHostArgumentCount(Handle); } }
        public ulong ReceiverId { get { return Native.NcHostReceiverId(Handle); } }
        public bool ResolveReceiver() { return Native.NcHostResolveReceiver(Handle) != IntPtr.Zero; }
        public bool ResolveObject(uint index) { return Native.NcHostResolveObject(Handle, index) != IntPtr.Zero; }
        public bool IsNull(uint index) { return Native.NcHostIsNull(Handle, index) != 0; }
        public long Int(uint index) { return Native.NcHostInt(Handle, index); }
        public double Float(uint index) { return Native.NcHostFloat(Handle, index); }
        public bool Bool(uint index) { return Native.NcHostBool(Handle, index) != 0; }
        public string String(uint index)
        { uint length; IntPtr text = Native.NcHostString(Handle, index, out length); return Result.ReadUtf8(text, length); }
        public ulong ObjectId(uint index) { return Native.NcHostObject(Handle, index); }
        public Scalar Argument(uint index) { return Scalar.Take(Native.NcHostArgument(Handle, index)); }
        public void Return(Scalar value) { using (var v = new ValueLease(value)) Native.NcHostReturnValue(Handle, v.Pointer); }
        public void ReturnInt(long value) { Native.NcHostReturnInt(Handle, value); }
        public void ReturnFloat(double value) { Native.NcHostReturnFloat(Handle, value); }
        public void ReturnBool(bool value) { Native.NcHostReturnBool(Handle, value ? 1 : 0); }
        public void ReturnString(string value)
        { byte[] bytes = Encoding.UTF8.GetBytes(value ?? throw new ArgumentNullException(nameof(value))); Native.NcHostReturnString(Handle, bytes, checked((uint)bytes.Length)); }
        public void ReturnObject(ulong id) { Native.NcHostReturnObject(Handle, id); }
        public void ReturnNull() { Native.NcHostReturnNull(Handle); }
        public void Fail(string message)
        { byte[] bytes = Encoding.UTF8.GetBytes(message ?? "Host callback failed"); Native.NcHostFail(Handle, bytes, checked((uint)bytes.Length)); }
        public Result Call(string name, params Scalar[] values)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(name ?? throw new ArgumentNullException(nameof(name)));
            using (var arguments = new Arguments(values))
                return Result.Take(Native.NcHostCallScript(Handle, bytes, checked((uint)bytes.Length), arguments.Pointer));
        }
        public Result Call(Scalar function, params Scalar[] values)
        {
            if (function.Kind != ScalarKind.Function) throw new ArgumentException("Value is not a function", nameof(function));
            using (var arguments = new Arguments(values)) return Result.Take(Native.NcHostCallFunction(Handle, function.Handle, arguments.Pointer));
        }
    }
}
