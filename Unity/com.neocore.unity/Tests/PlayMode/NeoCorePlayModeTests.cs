using System.Collections;
using NeoCore.Unity;
using NUnit.Framework;
using UnityEngine.TestTools;
using CoreProgram = NeoCore.Unity.Program;
using CoreResources = NeoCore.Unity.Resources;

namespace NeoCore.Tests
{
    public sealed class NeoCorePlayModeTests
    {
        [TearDown] public void ReleaseResources() { CoreResources.DisposeAll(); }

        [UnityTest] public IEnumerator SharedProgramInstancesKeepSuspensionIndependent()
        {
            using(var program=CoreProgram.Compile("int state=0;export int Tick(int step){state+=step;return state;}export int Wait(){sleep(0);state+=100;return state;}","frames.nc"))
            using(var first=program.CreateInstance())
            using(var second=program.CreateInstance())
            {
                Assert.AreEqual(ExecutionStatus.Suspended,first.Call("Wait").Status);
                for(int frame=1;frame<=3;++frame)
                {
                    Assert.AreEqual(frame,second.Call("Tick",Scalar.Int(1)).RequireCompleted().AsInt());yield return null;
                }
                Assert.IsTrue(first.IsSuspended);
                Assert.AreEqual(100,first.Resume(1).RequireCompleted().AsInt());
                Assert.AreEqual(3,second.Call("Tick",Scalar.Int(0)).RequireCompleted().AsInt());
                first.Reset();Assert.AreEqual(0,first.Call("Tick",Scalar.Int(0)).RequireCompleted().AsInt());
            }
        }

        [UnityTest] public IEnumerator InstructionSlicesContinueAcrossFrames()
        {
            using(var program=CoreProgram.Compile("export int Count(){int total=0;for(int i in 0,1000)total+=1;return total;}","budget.nc"))
            using(var instance=program.CreateInstance())
            {
                Result result=instance.CallLimited("Count",-1,64);int slices=0;
                while(result.Status==ExecutionStatus.Suspended&&slices++<1000)
                {yield return null;result=instance.Resume(0,64);}
                Assert.Greater(slices,0);Assert.AreEqual(1000,result.RequireCompleted().AsInt());Assert.IsFalse(instance.IsSuspended);
            }
        }
    }
}
