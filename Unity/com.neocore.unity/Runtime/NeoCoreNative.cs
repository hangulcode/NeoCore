using System;
using System.Runtime.InteropServices;

namespace NeoCore.Unity
{
    internal static partial class Native
    {
#if UNITY_IOS && !UNITY_EDITOR
        private const string Library = "__Internal";
#else
        private const string Library = "NeoCoreUnity";
#endif
        private const CallingConvention Convention = CallingConvention.Cdecl;

        [DllImport(Library, CallingConvention = Convention)] internal static extern IntPtr NcCompile(byte[] source, uint sourceLength, byte[] sourceName, uint nameLength, int debug, out IntPtr result);
        [DllImport(Library, CallingConvention = Convention)] internal static extern void NcProgramDestroy(IntPtr program);
        [DllImport(Library, CallingConvention = Convention)] internal static extern IntPtr NcInstanceCreate(IntPtr program, out IntPtr result);
        [DllImport(Library, CallingConvention = Convention)] internal static extern void NcInstanceDestroy(IntPtr instance);
        [DllImport(Library, CallingConvention = Convention)] internal static extern IntPtr NcRun(IntPtr instance);
        [DllImport(Library, CallingConvention = Convention)] internal static extern IntPtr NcCall(IntPtr instance, byte[] name, uint nameLength, IntPtr arguments);
        [DllImport(Library, CallingConvention = Convention)] internal static extern IntPtr NcResume(IntPtr instance, double deltaTime);
        [DllImport(Library, CallingConvention = Convention)] internal static extern IntPtr NcArgumentsCreate();
        [DllImport(Library, CallingConvention = Convention)] internal static extern void NcArgumentsDestroy(IntPtr arguments);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcArgumentsInt(IntPtr arguments, long value);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcArgumentsFloat(IntPtr arguments, double value);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcArgumentsBool(IntPtr arguments, int value);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcArgumentsString(IntPtr arguments, byte[] value, uint length);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcArgumentsNull(IntPtr arguments);
        [DllImport(Library, CallingConvention = Convention)] internal static extern void NcResultDestroy(IntPtr result);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcResultStatus(IntPtr result);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcResultKind(IntPtr result);
        [DllImport(Library, CallingConvention = Convention)] internal static extern long NcResultInt(IntPtr result);
        [DllImport(Library, CallingConvention = Convention)] internal static extern double NcResultFloat(IntPtr result);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcResultBool(IntPtr result);
        [DllImport(Library, CallingConvention = Convention)] internal static extern IntPtr NcResultString(IntPtr result, out uint length);
        [DllImport(Library, CallingConvention = Convention)] internal static extern IntPtr NcResultError(IntPtr result, out uint length);
        [DllImport(Library, CallingConvention = Convention)] internal static extern IntPtr NcResultSource(IntPtr result, out uint length);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcResultLine(IntPtr result);
        [DllImport(Library, CallingConvention = Convention)] internal static extern int NcResultColumn(IntPtr result);
    }
}
