using System;
using System.IO;
using NeoCore.Unity;

internal static class ManagedSmoke
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    private static void DropTemporary(Instance instance)
    {
        instance.Call("MakeList").RequireCompleted();
    }

    private static void CheckMigratedBoundaries()
    {
        Scalar retained=default(Scalar);
        using(var options=new CompileOptions())
        {
            ScriptType service=options.Object("Service");
            ScriptType point=options.Struct("Point");
            options.DefineFields(point,new ScriptField("name",ScriptTypes.String),new ScriptField("position",ScriptTypes.Vector3));
            ScriptType callbackType=options.Function(ScriptTypes.Int,ScriptTypes.Int);
            options.Array32(ScriptTypes.Float);
            options.DefineInt("BUILD_VALUE",7);
            options.DefineFloat("BUILD_FLOAT",-0.0);
            options.DefineString("BUILD_NAME","a\"b\nc");
            bool rejectedNul=false; try { options.DefineString("BAD_NUL","a\0b"); } catch(ArgumentException) { rejectedNul=true; }
            Require(rejectedNul,"unsupported define NUL is rejected rather than changed");
            bool alive=true;
            options.SetResolver((type,id)=>alive && id==17);
            options.Global("Host",service,17);
            options.Method(service,"Twice",ScriptTypes.Int,new[]{ScriptTypes.Int},call=>call.ReturnInt(call.Int(0)*2));
            options.Method(service,"Echo",point,new[]{point},call=>{ using(var value=call.Argument(0)) call.Return(value); });
            options.Method(service,"Save",ScriptTypes.Void,new[]{callbackType},call=>{ retained.Dispose(); retained=call.Argument(0); });
            options.Method(service,"Reenter",ScriptTypes.Int,new[]{callbackType},call=>{ using(var value=call.Argument(0)) call.ReturnInt(call.Call(value,Scalar.Int(6)).RequireCompleted().AsInt()); });
            options.SetModuleLoader((name,importer)=>new ModuleSource("public int Add(int a,int b){return a+b;}","maths.nc"));
            string source="import maths as m;\n"
                +"export Vector3 position=Vector3(1,2,3); position.x=4.0;\n"
                +"export int Twice(int n){return Host.Twice(m.Add(n,1));}\n"
                +"export int BuildValue(){return BUILD_VALUE;}\n"
                +"export float BuildFloat(){return BUILD_FLOAT;}\n"
                +"export string BuildName(){return BUILD_NAME;}\n"
                +"export Point Echo(Point p){return Host.Echo(p);}\n"
                +"export Vector3 V(Vector3 v){return v;}\n"
                +"export Quaternion Q(Quaternion v){return v;}\n"
                +"export list<int> MakeList(){return [3,5];}\n"
                +"export array<float> MakeArray(){return array<float>(3);}\n"
                +"export int Read(list<int> a){return a[0];}\n"
                +"export void Save(){int base=10; Host.Save(fn(int n)->int {return base+n;});}\n"
                +"export int Reenter(){return Host.Reenter(fn(int n)->int {return n+2;});}\n"
                +"export int Loop(int n){int s=0; for(int i in 0,n){s+=i;} return s;}\n";
            using(var program=NeoCore.Unity.Program.Compile(source,"migrated.nc",true,options))
            using(var instance=program.CreateInstance())
            {
                options.Dispose();
                GC.Collect(); GC.WaitForPendingFinalizers();
                Require(instance.Call("Twice",Scalar.Int(4)).RequireCompleted().AsInt()==10,"schema callback roots after options disposal");
                Require(instance.Call("BuildValue").RequireCompleted().AsInt()==7,"typed integer define");
                Require(BitConverter.DoubleToInt64Bits(instance.Call("BuildFloat").RequireCompleted().AsFloat())<0,"define preserves signed zero");
                Require(instance.Call("BuildName").RequireCompleted().AsString()=="a\"b\nc","typed string define escapes");
                Require(instance.Call("Reenter").RequireCompleted().AsInt()==8,"typed script reentry");
                using(var function=program.FindFunction("Twice"))
                    Require(instance.Call(function,Scalar.Int(2)).RequireCompleted().AsInt()==6,"cached named function handle");
                using(var value=program.CreateValue("Point",Scalar.String("a\0b"),Scalar.Vector3(1,2,3)))
                using(var result=instance.Call("Echo",value).RequireCompleted())
                {
                    result.Set(0,Scalar.String("changed"));
                    Require(value.At(0).AsString()=="a\0b","struct callback copies values");
                    bool rejectedJsonNul=false; try { program.ToJson(value); } catch(InvalidOperationException) { rejectedJsonNul=true; }
                    Require(rejectedJsonNul,"JSON rejects NUL while host strings retain it");
                    value.Set(0,Scalar.String("a\"b"));
                    string json=program.ToJson(value,true);
                    using(var decoded=program.ParseJson("Point",json))
                        Require(decoded.At(0).AsString()=="a\"b" && decoded.At(1).AsVector3().z==3,"typed JSON struct/vector roundtrip");
                }
                Require(instance.Call("V",Scalar.Vector3(1,2,3)).RequireCompleted().AsVector3().y==2,"Vector3 managed conversion");
                var q=instance.Call("Q",Scalar.Quaternion(1,2,3,4)).RequireCompleted().AsQuaternion();
                Require(q.w==1 && q.x==2 && q.z==4,"Quaternion wxyz boundary");
                Require(instance.Call("V",Scalar.Vector4(1,2,3,4)).Status==ExecutionStatus.Failed,"exact vector signature rejects prefix conversion");
                using(var array=instance.Call("MakeArray").RequireCompleted())
                {
                    array.CopyFrom(new[]{1.0,2.0,3.0});
                    var numbers=new double[2]; array.CopyTo(numbers,1);
                    Require(numbers[0]==2 && numbers[1]==3,"dense array bulk copy");
                    bool wrong=false; try { array.CopyFrom(new float[]{1,2,3}); } catch(InvalidOperationException) { wrong=true; }
                    Require(wrong,"bulk copy rejects storage width mismatch");
                }
                using(var compact=program.CreateValue("array32<float>"))
                {
                    compact.Resize(2); compact.CopyFrom(new float[]{1.5f,2.5f});
                    var numbers=new float[2]; compact.CopyTo(numbers);
                    Require(numbers[0]==1.5f && numbers[1]==2.5f,"compact float32 bulk copy");
                    bool overflow=false; try { using(program.ParseJson("array32<float>","[1e100]")){} } catch(InvalidOperationException){overflow=true;}
                    Require(overflow,"JSON float32 overflow rejected");
                }
                Require(instance.ReadGlobal("position").AsVector3().x==4,"boxed vector global read");
                instance.WriteGlobal("position",Scalar.Vector3(9,8,7));
                Require(instance.ReadGlobal("position").AsVector3().x==9,"boxed vector global write");
                using(var list=instance.Call("MakeList").RequireCompleted())
                using(var alias=list.Clone())
                {
                    Require(list.SameStorage(alias),"collection shared identity");
                    alias.Set(0,Scalar.Int(99)); list.Reserve(32); list.Append(Scalar.Int(7)); list.RemoveAt(1);
                    Require(instance.Call("Read",list).RequireCompleted().AsInt()==99 && list.Count==2,"typed collection host mutations");
                    bool rejected=false; try { list.Resize(ulong.MaxValue); } catch(InvalidOperationException) { rejected=true; }
                    Require(rejected && list.Count==2,"wide count rejected before allocation");
                }
                instance.Call("Save").RequireCompleted();
                Require(instance.Call(retained,Scalar.Int(3)).RequireCompleted().AsInt()==13,"retained script closure");
                Require(instance.CallLimited("Loop",-1,16,Scalar.Int(100)).Status==ExecutionStatus.Suspended,"instruction budget suspension");
                Result budget;
                do { budget=instance.Resume(0,16); } while(budget.Status==ExecutionStatus.Suspended);
                Require(budget.RequireCompleted().AsInt()==4950,"budget resumption");
                byte[] image=program.SaveImage();
                Require(image.Length>0,"typed image export");
                long before=NeoCore.Unity.Resources.AllocationStat(2);
                for(int i=0;i<80;++i) DropTemporary(instance);
                GC.Collect(); GC.WaitForPendingFinalizers(); NeoCore.Unity.Resources.Drain();
                program.TrimMemory(true);
                long after=NeoCore.Unity.Resources.AllocationStat(2);
                Require(after<=before+1,"discarded managed values release on owning thread: "+before+" -> "+after);
                alive=false;
                Require(instance.Call("Twice",Scalar.Int(1)).Status==ExecutionStatus.Failed,"destroyed native ID rejected");
                alive=true; instance.Reset();
                Require(instance.Call(retained,Scalar.Int(1)).Status==ExecutionStatus.Failed,"reset invalidates captured closure");
                retained.Dispose();
            }
        }
        using(var program=NeoCore.Unity.Program.Compile("export int Value(){return 42;}","image.nc"))
        using(var loaded=NeoCore.Unity.Program.LoadImage(program.SaveImage()))
        using(var instance=loaded.CreateInstance()) Require(instance.Call("Value").RequireCompleted().AsInt()==42,"typed image roundtrip");
        string printed=null;
        Diagnostics.SetLogSink(text=>printed=text,null);
        using(var program=NeoCore.Unity.Program.Compile("print(42);","log.nc"))
        using(var instance=program.CreateInstance()) instance.Run().RequireCompleted();
        Require(printed!=null && printed.Contains("42"),"managed log sink");
        Diagnostics.ClearLogSink();
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
            CheckMigratedBoundaries();
            Console.WriteLine("NeoCore managed smoke passed: typed schema, values, closures, JSON, modules, images, vectors, budgets, ownership, cleanup");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
        finally { NeoCore.Unity.Resources.DisposeAll(); }
    }
}
