#if UNITY_EDITOR
using System;
using NeoCore.Unity;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

[InitializeOnLoad]
public static class NeoCoreUnitySmoke
{
    private const string Key = "NeoCoreSmokeStage";
    private static int frames;
    private static int previousFrame = -1;

    static NeoCoreUnitySmoke()
    {
        if (SessionState.GetInt(Key, 0) == 2) SessionState.SetInt(Key, 4);
        EditorApplication.update += Tick;
        EditorApplication.playModeStateChanged += state =>
        {
            if (state == PlayModeStateChange.EnteredEditMode && SessionState.GetInt(Key, 0) == 3)
            {
                SessionState.SetInt(Key, 0);
                Debug.Log("NEOCORE_UNITY_SMOKE_PASSED: importer, managed boundary, PlayMode, domain reload, cleanup");
                EditorApplication.Exit(0);
            }
        };
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    public static void Run()
    {
        try
        {
            var source = AssetDatabase.LoadAssetAtPath<TextAsset>("Assets/Basic/demo.nc");
            Require(source != null && source.text.Contains("export bool Update"), ".nc importer must produce the typed TextAsset");
            using (var program = NeoCore.Unity.Program.Compile(source.text, "demo.nc"))
            using (var first = program.CreateInstance())
            using (var second = program.CreateInstance())
            {
                Require(first.Call("Update", Scalar.Float(0.125), Scalar.Float(0.5)).RequireCompleted().AsBool(), "first update must return true");
                Require(first.Call("Ticks").RequireCompleted().AsInt() == 1, "managed int64 result");
                Require(second.Call("Ticks").RequireCompleted().AsInt() == 0, "managed independent instance");
                Require(first.Call("PositionY").RequireCompleted().AsFloat() == 1.125, "managed float result");
                Require(first.Call("Update", Scalar.String("wrong"), Scalar.Float(0)).Status == ExecutionStatus.Failed, "typed argument rejection");
                program.Dispose();
                Require(first.Call("Ticks").RequireCompleted().AsInt() == 1, "instance retains program");
            }
            using (var program = NeoCore.Unity.Program.Compile("export string Echo(string x) { return x; } export int Wait() { sleep(0); return 42; }", "boundary.nc"))
            using (var instance = program.CreateInstance())
            {
                Require(instance.Call("Echo", Scalar.String("UTF-8 \u2603")).RequireCompleted().AsString() == "UTF-8 \u2603", "managed UTF-8 ownership");
                Require(instance.Call("Wait").Status == ExecutionStatus.Suspended, "managed suspended status");
                Require(instance.Resume().RequireCompleted().AsInt() == 42, "managed resumed value");
            }
            bool rejected = false;
            try { using (NeoCore.Unity.Program.Compile("var dynamic = 1;", "reject.nc")) { } }
            catch (InvalidOperationException) { rejected = true; }
            Require(rejected, "legacy syntax must not compile");
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            var target = new GameObject("NeoCoreSmoke");
            var demo = target.AddComponent<NeoCoreDemo>();
            demo.Script = source;
            EditorSceneManager.SaveScene(target.scene, "Assets/NeoCoreSmoke.unity");
            SessionState.SetInt(Key, 1);
            EditorApplication.EnterPlaymode();
        }
        catch (Exception error) { Fail(error); }
    }

    private static void Tick()
    {
        int stage = SessionState.GetInt(Key, 0);
        if ((stage != 1 && stage != 4) || !EditorApplication.isPlaying || EditorApplication.isCompiling) return;
        if (Time.frameCount == previousFrame) return;
        previousFrame = Time.frameCount;
        if (++frames < 12) return;
        try
        {
            var target = GameObject.Find("NeoCoreSmoke");
            Require(target != null, "PlayMode object exists");
            var demo = target.GetComponent<NeoCoreDemo>();
            Require(demo != null && demo.enabled, "typed demo remains enabled in PlayMode");
            Vector3 actual = target.transform.localPosition;
            Require(Math.Abs(actual.x * actual.x + actual.z * actual.z - 4) < 0.0001, "actual Update drives wobble");
            demo.Step(0.125, 0.5);
            actual = target.transform.localPosition;
            Require(Math.Abs(actual.x - (float)(Math.Sin(0.5) * 2)) < 0.000001, "deterministic Unity x");
            Require(actual.y == 1.125f, "deterministic Unity y");
            Require(Math.Abs(actual.z - (float)(Math.Cos(0.5) * 2)) < 0.000001, "deterministic Unity z");
            NeoCore.Unity.Resources.DisposeAll();
            demo.Step(0.125, 0);
            Require(target.transform.localPosition == new Vector3(0, 1.125f, 2), "reboot after resource cleanup");
            if (stage == 1)
            {
                SessionState.SetInt(Key, 2);
                frames = 0;
                Debug.Log("NEOCORE_UNITY_SMOKE_RELOAD: requesting script-domain reload in PlayMode");
                EditorUtility.RequestScriptReload();
            }
            else
            {
                SessionState.SetInt(Key, 3);
                EditorApplication.ExitPlaymode();
            }
        }
        catch (Exception error) { Fail(error); }
    }

    private static void Fail(Exception error)
    {
        SessionState.SetInt(Key, 0);
        NeoCore.Unity.Resources.DisposeAll();
        Debug.LogException(error);
        EditorApplication.Exit(1);
    }
}
#endif
