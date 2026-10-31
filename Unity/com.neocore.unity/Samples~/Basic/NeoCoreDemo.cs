using System;
using NeoCore.Unity;
using UnityEngine;

public sealed class NeoCoreDemo : MonoBehaviour
{
    public TextAsset Script;
    private NeoCore.Unity.Program program;
    private Instance instance;

    private void OnEnable()
    {
        Boot();
    }

    private void Boot()
    {
        Teardown();
        try
        {
            if (Script == null) throw new InvalidOperationException("Assign the imported demo.nc asset to Script");
            program = NeoCore.Unity.Program.Compile(Script.text, Script.name + ".nc");
            instance = program.CreateInstance();
            instance.Run().RequireCompleted();
            Debug.Log("[neocore] " + name + " is now driven by demo.nc");
        }
        catch (Exception error)
        {
            Debug.LogError("[neocore] " + error.Message);
            Teardown();
            enabled = false;
        }
    }

    public void Step(double deltaTime, double time)
    {
        if (instance == null || !instance.IsAlive)
        {
            Boot();
            if (instance == null) return;
        }
        bool firstTick = instance.Call("Update", Scalar.Float(deltaTime), Scalar.Float(time)).RequireCompleted().AsBool();
        transform.localPosition = new Vector3(
            (float)instance.Call("PositionX").RequireCompleted().AsFloat(),
            (float)instance.Call("PositionY").RequireCompleted().AsFloat(),
            (float)instance.Call("PositionZ").RequireCompleted().AsFloat());
        if (firstTick) Debug.Log("[neocore] typed NeoCore script is driving this transform");
    }

    private void Update()
    {
        try { Step(Time.deltaTime, Time.time); }
        catch (Exception error)
        {
            Debug.LogError("[neocore] Update failed: " + error.Message);
            enabled = false;
        }
    }

    private void OnDisable() { Teardown(); }
    private void OnDestroy() { Teardown(); }
    private void Teardown()
    {
        if (instance != null) instance.Dispose();
        if (program != null) program.Dispose();
        instance = null;
        program = null;
    }
}
