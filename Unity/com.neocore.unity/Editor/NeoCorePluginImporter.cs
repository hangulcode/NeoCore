using System;
using UnityEditor;

namespace NeoCore.Unity.Editor
{
    internal sealed class NeoCorePluginImporter : AssetPostprocessor
    {
        private const string PluginRoot = "com.neocore.unity/Runtime/Plugins/";
        private static readonly string[] Plugins = {
            "x86_64/NeoCoreUnity.dll",
            "Android/arm64-v8a/libNeoCoreUnity.so",
            "Android/armeabi-v7a/libNeoCoreUnity.so",
            "Android/x86_64/libNeoCoreUnity.so",
            "iOS/libNeoCoreUnity.a"
        };
        private static readonly BuildTarget[] Platforms = {
            BuildTarget.StandaloneWindows64, BuildTarget.StandaloneLinux64,
            BuildTarget.StandaloneOSX, BuildTarget.Android, BuildTarget.iOS
        };

        private static bool Settings(string path, out BuildTarget target, out string cpu)
        {
            target = BuildTarget.NoTarget;
            cpu = null;
            path = path.Replace('\\', '/');
            int root = path.IndexOf(PluginRoot, StringComparison.Ordinal);
            if (root < 0) return false;
            string relative = path.Substring(root + PluginRoot.Length);
            switch (relative)
            {
                case "x86_64/NeoCoreUnity.dll": target = BuildTarget.StandaloneWindows64; cpu = "x86_64"; break;
                case "Android/arm64-v8a/libNeoCoreUnity.so": target = BuildTarget.Android; cpu = "ARM64"; break;
                case "Android/armeabi-v7a/libNeoCoreUnity.so": target = BuildTarget.Android; cpu = "ARMv7"; break;
                case "Android/x86_64/libNeoCoreUnity.so": target = BuildTarget.Android; cpu = "x86_64"; break;
                case "iOS/libNeoCoreUnity.a": target = BuildTarget.iOS; break;
                default: return false;
            }
            return true;
        }

        private static bool IsConfigured(PluginImporter importer, BuildTarget target, string cpu)
        {
            bool editor = target == BuildTarget.StandaloneWindows64;
            if (importer.GetCompatibleWithAnyPlatform() || importer.GetCompatibleWithEditor() != editor) return false;
            foreach (BuildTarget platform in Platforms)
                if (importer.GetCompatibleWithPlatform(platform) != (platform == target)) return false;
            if (cpu != null && importer.GetPlatformData(target, "CPU") != cpu) return false;
            return !editor || (importer.GetEditorData("OS") == "Windows" && importer.GetEditorData("CPU") == "x86_64");
        }

        private static void Configure(PluginImporter importer, BuildTarget target, string cpu)
        {
            // 이전 임포트 설정을 지운 뒤 해당 바이너리의 플랫폼만 켠다.
            importer.SetCompatibleWithAnyPlatform(false);
            importer.SetCompatibleWithEditor(target == BuildTarget.StandaloneWindows64);
            foreach (BuildTarget platform in Platforms)
                importer.SetCompatibleWithPlatform(platform, platform == target);
            if (cpu != null) importer.SetPlatformData(target, "CPU", cpu);
            if (target == BuildTarget.StandaloneWindows64)
            {
                importer.SetEditorData("OS", "Windows");
                importer.SetEditorData("CPU", "x86_64");
            }
        }

        private void OnPreprocessAsset()
        {
            var importer = assetImporter as PluginImporter;
            BuildTarget target;
            string cpu;
            if (importer != null && Settings(assetPath, out target, out cpu)) Configure(importer, target, cpu);
        }

        [InitializeOnLoadMethod]
        private static void Verify()
        {
            foreach (string relative in Plugins)
            {
                string path = "Packages/" + PluginRoot + relative;
                var importer = AssetImporter.GetAtPath(path) as PluginImporter;
                BuildTarget target;
                string cpu;
                if (importer == null || !Settings(path, out target, out cpu) || IsConfigured(importer, target, cpu)) continue;
                Configure(importer, target, cpu);
                importer.SaveAndReimport();
            }
        }
    }
}
