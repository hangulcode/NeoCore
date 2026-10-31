using System;
using UnityEditor;

namespace NeoCore.Unity.Editor
{
    internal sealed class NeoCorePluginImporter : AssetPostprocessor
    {
        private const string Path = "Packages/com.neocore.unity/Runtime/Plugins/x86_64/NeoCoreUnity.dll";

        private static void Configure(PluginImporter importer)
        {
            importer.SetCompatibleWithAnyPlatform(false);
            importer.SetCompatibleWithEditor(true);
            importer.SetEditorData("OS", "Windows");
            importer.SetEditorData("CPU", "x86_64");
            importer.SetCompatibleWithPlatform(BuildTarget.StandaloneWindows64, true);
            importer.SetPlatformData(BuildTarget.StandaloneWindows64, "CPU", "x86_64");
            importer.SetCompatibleWithPlatform(BuildTarget.StandaloneLinux64, false);
            importer.SetCompatibleWithPlatform(BuildTarget.StandaloneOSX, false);
            importer.SetCompatibleWithPlatform(BuildTarget.Android, false);
            importer.SetCompatibleWithPlatform(BuildTarget.iOS, false);
        }

        private void OnPreprocessAsset()
        {
            var importer = assetImporter as PluginImporter;
            if (importer != null && assetPath.EndsWith("com.neocore.unity/Runtime/Plugins/x86_64/NeoCoreUnity.dll", StringComparison.Ordinal))
                Configure(importer);
        }

        [InitializeOnLoadMethod]
        private static void Verify()
        {
            var importer = AssetImporter.GetAtPath(Path) as PluginImporter;
            if (importer == null || (!importer.GetCompatibleWithAnyPlatform()
                && importer.GetCompatibleWithEditor()
                && importer.GetCompatibleWithPlatform(BuildTarget.StandaloneWindows64))) return;
            Configure(importer);
            importer.SaveAndReimport();
        }
    }
}
