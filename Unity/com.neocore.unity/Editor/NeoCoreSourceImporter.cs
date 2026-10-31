using System.IO;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace NeoCore.Unity.Editor
{
    [ScriptedImporter(1, "nc")]
    internal sealed class NeoCoreSourceImporter : ScriptedImporter
    {
        public override void OnImportAsset(AssetImportContext context)
        {
            var source = new TextAsset(File.ReadAllText(context.assetPath));
            source.name = Path.GetFileNameWithoutExtension(context.assetPath);
            context.AddObjectToAsset("source", source);
            context.SetMainObject(source);
        }
    }
}
