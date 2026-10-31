using System;
using AOT;

namespace NeoCore.Unity
{
    public static class Diagnostics
    {
        private static Action<string> printSink;
        private static Action<string> errorSink;
        private static readonly Native.LogCallback callback=OnLog;
        private static bool installed;
        [UnityEngine.RuntimeInitializeOnLoadMethod(UnityEngine.RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void ResetSubsystem() { Resources.DisposeAll(); ClearLogSink(); }

        public static void SetLogSink(Action<string> print, Action<string> error)
        {
            Native.CheckVersion();
            if(Native.NcSetLogHandler(callback,IntPtr.Zero,null)==0) throw new InvalidOperationException("Could not install log sink");
            printSink=print; errorSink=error; installed=true;
        }
        public static void ClearLogSink()
        {
            if(installed) Native.NcSetLogHandler(null,IntPtr.Zero,null);
            installed=false; printSink=null; errorSink=null;
        }
        [MonoPInvokeCallback(typeof(Native.LogCallback))]
        private static void OnLog(IntPtr user, int error, IntPtr text, uint length)
        {
            try { (error!=0?errorSink:printSink)?.Invoke(Result.ReadUtf8(text,length)); }
            catch { }
        }
    }
}
