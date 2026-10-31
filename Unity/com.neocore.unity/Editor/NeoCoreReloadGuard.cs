using UnityEditor;

namespace NeoCore.Unity.Editor
{
    [InitializeOnLoad]
    internal static class NeoCoreReloadGuard
    {
        static NeoCoreReloadGuard()
        {
            AssemblyReloadEvents.beforeAssemblyReload += Cleanup;
            EditorApplication.quitting += Cleanup;
            EditorApplication.playModeStateChanged += change =>
            {
                if (change == PlayModeStateChange.EnteredEditMode) Cleanup();
            };
        }
        private static void Cleanup() { Resources.DisposeAll(); Diagnostics.ClearLogSink(); }
    }
}
