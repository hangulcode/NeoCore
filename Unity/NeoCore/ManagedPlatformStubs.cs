// Unity가 없는 CI에서 C ABI의 관리 경계만 검사한다. 배포 패키지는 실제 Unity 형식을 참조한다.
using System;
namespace UnityEngine
{
    public struct Vector2 { public float x,y; public Vector2(float x,float y) { this.x=x;this.y=y; } }
    public struct Vector3 { public float x,y,z; public Vector3(float x,float y,float z) { this.x=x;this.y=y;this.z=z; } }
    public struct Vector4 { public float x,y,z,w; public Vector4(float x,float y,float z,float w) { this.x=x;this.y=y;this.z=z;this.w=w; } }
    public struct Quaternion { public float x,y,z,w; public Quaternion(float x,float y,float z,float w) { this.x=x;this.y=y;this.z=z;this.w=w; } }
    public enum RuntimeInitializeLoadType { SubsystemRegistration }
    [AttributeUsage(AttributeTargets.Method)] public sealed class RuntimeInitializeOnLoadMethodAttribute:Attribute
    { public RuntimeInitializeOnLoadMethodAttribute(RuntimeInitializeLoadType value) {} }
}
namespace AOT
{
    [AttributeUsage(AttributeTargets.Method)] public sealed class MonoPInvokeCallbackAttribute:Attribute
    { public MonoPInvokeCallbackAttribute(Type type) {} }
}
