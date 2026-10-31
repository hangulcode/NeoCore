using UnityEditor;

namespace NeoCore.Unity.Editor
{
    [InitializeOnLoad]
    internal static class NeoCoreReloadGuard
    {
        static NeoCoreReloadGuard()
        {
            AssemblyReloadEvents.beforeAssemblyReload += Resources.DisposeAll;
            EditorApplication.quitting += Resources.DisposeAll;
            EditorApplication.playModeStateChanged += change =>
            {
                if (change == PlayModeStateChange.EnteredEditMode) Resources.DisposeAll();
            };
        }
    }
}
