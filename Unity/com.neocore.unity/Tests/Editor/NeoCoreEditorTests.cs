using System;
using System.Threading;
using NeoCore.Unity;
using NUnit.Framework;
using UnityEngine;
using CoreProgram = NeoCore.Unity.Program;
using CoreResources = NeoCore.Unity.Resources;

namespace NeoCore.Tests
{
    public sealed class NeoCoreEditorTests
    {
        [TearDown] public void ReleaseResources() { CoreResources.DisposeAll(); }

        [Test] public void ScalarAndVectorTypesAreExact()
        {
            using(var program=CoreProgram.Compile("export int I(int x){return x;}export float F(float x){return x;}export string S(string x){return x;}export Vector3 V(Vector3 x){return x;}export Quaternion Q(Quaternion x){return x;}","types.nc"))
            using(var instance=program.CreateInstance())
            {
                Assert.AreEqual(long.MaxValue,instance.Call("I",Scalar.Int(long.MaxValue)).RequireCompleted().AsInt());
                double minusZero=BitConverter.Int64BitsToDouble(long.MinValue);
                Assert.AreEqual(long.MinValue,BitConverter.DoubleToInt64Bits(instance.Call("F",Scalar.Float(minusZero)).RequireCompleted().AsFloat()));
                Assert.AreEqual("a\0b",instance.Call("S",Scalar.String("a\0b")).RequireCompleted().AsString());
                Assert.AreEqual(new Vector3(1,2,3),instance.Call("V",Scalar.Vector3(1,2,3)).RequireCompleted().AsVector3());
                Assert.AreEqual(new Quaternion(2,3,4,1),instance.Call("Q",Scalar.Quaternion(1,2,3,4)).RequireCompleted().AsQuaternion());
                Assert.AreEqual(ExecutionStatus.Failed,instance.Call("Q",Scalar.Vector4(1,2,3,4)).Status);
            }
        }

        [Test] public void OwnedCollectionsAndClosuresPreserveTheirContracts()
        {
            using(var program=CoreProgram.Compile("export list<int> values=[1,2];export fn()->int Make(){int n=4;return fn()->int{n+=1;return n;};}","owned.nc"))
            using(var instance=program.CreateInstance())
            using(var values=instance.ReadGlobal("values"))
            using(var alias=instance.ReadGlobal("values"))
            using(var closure=instance.Call("Make").RequireCompleted())
            {
                Assert.IsTrue(values.SameStorage(alias));
                values.Set(0,Scalar.Int(7)); Assert.AreEqual(7,alias.At(0).AsInt());
                Assert.Throws<InvalidOperationException>(()=>values.Append(Scalar.String("wrong")));
                Assert.Throws<InvalidOperationException>(()=>values.Resize(ulong.MaxValue));
                Assert.AreEqual(5,instance.Call(closure).RequireCompleted().AsInt());
                Assert.AreEqual(6,instance.Call(closure).RequireCompleted().AsInt());
                instance.Reset(); Assert.AreEqual(7,values.At(0).AsInt());
                Assert.AreEqual(ExecutionStatus.Failed,instance.Call(closure).Status);
            }
        }

        [Test] public void TypedJsonPreservesStructCopiesAndInt64()
        {
            using(var program=CoreProgram.Compile("struct Payload{int id;string name;};export Payload Echo(Payload x){x.id+=1;return x;}","json.nc"))
            using(var instance=program.CreateInstance())
            using(var source=program.ParseJson("Payload","{\"id\":9007199254740993,\"name\":\"sample\"}"))
            using(var result=instance.Call("Echo",source).RequireCompleted())
            using(var restored=program.ParseJson("Payload",program.ToJson(result,true)))
            {
                Assert.AreEqual(9007199254740993L,source.At(0).AsInt());
                Assert.AreEqual(9007199254740994L,restored.At(0).AsInt());
                Assert.Throws<InvalidOperationException>(()=>program.ParseJson("Payload","{\"id\":1}"));
            }
        }

        [Test] public void ImagesPreserveModulesWithoutRecompiling()
        {
            int calls=0;
            using(var options=new CompileOptions())
            {
                options.SetModuleLoader((name,importer)=>{++calls;return new ModuleSource("public int Get(){return 41;}","module.nc");});
                byte[] image;
                using(var program=CoreProgram.Compile("import source;export int Run(){return source.Get()+1;}","image.nc",true,options)) image=program.SaveImage();
                Assert.AreEqual(1,calls);
                options.SetModuleLoader((name,importer)=>{throw new InvalidOperationException("must not recompile image");});
                using(var loaded=CoreProgram.LoadImage(image,options))
                using(var instance=loaded.CreateInstance()) Assert.AreEqual(42,instance.Call("Run").RequireCompleted().AsInt());
                image[image.Length-1]^=1;
                Assert.Throws<InvalidOperationException>(()=>CoreProgram.LoadImage(image,options));
            }
        }

        [Test] public void HostIdsAndCallbackReentryRemainChecked()
        {
            bool alive=true; long position=10; Instance instance=null; HostCall expired=null;
            using(var options=new CompileOptions())
            {
                var type=options.Object("GameObjectType");
                options.SetResolver((kind,id)=>alive&&id==7);
                options.Property(type,"Position",ScriptTypes.Int,c=>c.ReturnInt(position),c=>position=c.Int(0));
                options.Method(type,"Invoke",ScriptTypes.Int,new[]{options.Function(ScriptTypes.Int)},call=>
                {
                    expired=call; Assert.IsTrue(instance.IsRunning);
                    Assert.Throws<InvalidOperationException>(()=>instance.Reset());
                    Assert.Throws<InvalidOperationException>(()=>instance.Dispose());
                    using(var function=call.Argument(0)) call.Return(call.Call(function).RequireCompleted());
                });
                options.Global("GameObject",type,7);
                using(var program=CoreProgram.Compile("export int Run(){GameObject.Position+=1;return GameObject.Invoke(fn()->int{return GameObject.Position;});}","host.nc",true,options))
                using(instance=program.CreateInstance())
                {
                    Assert.AreEqual(11,instance.Call("Run").RequireCompleted().AsInt());
                    Assert.Throws<ObjectDisposedException>(()=>{var count=expired.ArgumentCount;});
                    alive=false;Assert.AreEqual(ExecutionStatus.Failed,instance.Call("Run").Status);
                }
            }
        }

        [Test] public void MockHttpCopiesDataAndExpiresBorrowedRequest()
        {
            int calls=0; HttpRequest borrowed=null;
            using(var options=new CompileOptions())
            {
                options.SetHttpTransport(request=>{borrowed=request;Interlocked.Increment(ref calls);return new HttpResponse(request.Url=="https://sample.invalid/test","ok");});
                using(var program=CoreProgram.Compile("import system;bool result=false;void Done(bool ok,string text){result=ok&&text==\"ok\";}AsyncRequest r=system.async_create();r.get(5000,\"https://sample.invalid/test\",Done);r.wait();r.close();export bool Verify(){return result;}","http.nc",true,options))
                using(var instance=program.CreateInstance()) Assert.IsTrue(instance.Call("Verify").RequireCompleted().AsBool());
            }
            Assert.AreEqual(1,calls);
            Assert.Throws<ObjectDisposedException>(()=>{var cancelled=borrowed.IsCancellationRequested;});
        }

        [Test] public void ReloadCleanupIsIdempotent()
        {
            for(int pass=0;pass<3;++pass)
            {
                var program=CoreProgram.Compile("export int Run(){return 42;}","reload.nc");
                var instance=program.CreateInstance();Assert.AreEqual(42,instance.Call("Run").RequireCompleted().AsInt());
                CoreResources.DisposeAll();CoreResources.DisposeAll();Assert.IsFalse(instance.IsAlive);
            }
        }
    }
}
