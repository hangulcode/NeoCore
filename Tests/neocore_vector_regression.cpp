#include "NeoCore.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

static int checks = 0;
static int failures = 0;

static void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
    }
}

static NeoCore::Result Invoke(const char* source, const char* label, bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, label, debug);
    Check(program != nullptr, std::string(label) + " compile: " + error.message);
    if (!program)
    {
        NeoCore::Result result;
        result.error = error;
        return result;
    }
    NeoCore::Result result = program->Call("Probe");
    Check(result.ok(), std::string(label) + " execute: " + result.error.message);
    return result;
}

static void ExpectFloat(const char* source, double expected, const char* label, bool debug)
{
    const NeoCore::Result result = Invoke(source, label, debug);
    const bool floating = result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Float;
    const double actual = floating ? result.value.asFloat() : 0;
    const bool same = std::isnan(expected) ? std::isnan(actual) :
        actual == expected && (expected != 0 || std::signbit(actual) == std::signbit(expected));
    Check(floating && same, std::string(label) + " expected " + std::to_string(expected) +
        ", got " + std::to_string(actual));
}

static void ExpectBool(const char* source, bool expected, const char* label, bool debug)
{
    const NeoCore::Result result = Invoke(source, label, debug);
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Bool && result.value.asBool() == expected,
        std::string(label) + " result");
}

static void CheckValues(bool debug)
{
    const std::string bomSource = "\xEF\xBB\xBF" "export float Probe(){Vector3 v=Vector3(1,2,3);return v.z;}";
    ExpectFloat(bomSource.c_str(), 3, "UTF-8 BOM survives internal vector import", debug);
    ExpectBool("export bool Probe(){Vector2 a;Vector3 b;Vector4 c;Quaternion q;return a.x==0.0&&a.y==0.0&&b.x==0.0&&b.y==0.0&&b.z==0.0&&c.x==0.0&&c.y==0.0&&c.z==0.0&&c.w==0.0&&q.w==1.0&&q.x==0.0&&q.y==0.0&&q.z==0.0;}",
        true, "default zero vectors and identity quaternion", debug);
    ExpectBool("export bool Probe(){Vector2 a=Vector2();Vector3 b=Vector3();Vector4 c=Vector4();Quaternion q=Quaternion();return a.x==0.0&&b.z==0.0&&c.w==0.0&&q.w==1.0&&q.z==0.0;}",
        true, "empty vector constructors", debug);
    ExpectFloat("export float Probe(){Vector2 a=Vector2(2,3);return a.x*10+a.y;}",
        23, "vector2 positional construction", debug);
    ExpectFloat("export float Probe(){Vector3 a=Vector3(2,3.5,4);return a.x*100+a.y*10+a.z;}",
        239, "vector3 positional construction and numeric promotion", debug);
    ExpectFloat("export float Probe(){Vector4 a=Vector4(1,2,3,4);return a.x*1000+a.y*100+a.z*10+a.w;}",
        1234, "vector4 xyzw component order", debug);
    ExpectFloat("export float Probe(){Quaternion q=Quaternion(1,2,3,4);return q.w*1000+q.x*100+q.y*10+q.z;}",
        1234, "quaternion wxyz component order", debug);
    ExpectFloat("Vector3 Make(){return Vector3(2,3,4);}export float Probe(){return Make().z+Vector2(1,5).y;}",
        9, "temporary vector component reads", debug);
    ExpectFloat("export float Probe(){Vector4 v;v.x=1;v.y=2;v.z=3;v.w=4;return v.x+v.y+v.z+v.w;}",
        10, "all vector components writable", debug);
    ExpectFloat("export float Probe(){Quaternion q;q.w=2;q.x=3;q.y=4;q.z=5;return q.w*1000+q.x*100+q.y*10+q.z;}",
        2345, "quaternion named writes preserve wxyz", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(1,2,3);v.x+=4;v.x*=3;v.x-=1;v.x/=2;return v.x+v.y+v.z;}",
        12, "component compound arithmetic", debug);
    ExpectFloat("export float Probe(){Vector2 v=Vector2(2,0);float a=v.x++;float b=++v.x;float c=v.x--;float d=--v.x;return a*10000+b*1000+c*100+d*10+v.x;}",
        24422, "component prefix and postfix results", debug);
    ExpectBool("export bool Probe(){Vector3? v=null;Quaternion? q=Quaternion();return v==null&&q!=null;}",
        true, "nullable vector values compare with null", debug);
    ExpectBool("export bool Probe(){Vector3? v=null;Quaternion? q=Quaternion();return null==v&&null!=q;}",
        true, "null comparisons with nullable vectors are symmetric", debug);
}

static void CheckCopiesAndReferences(bool debug)
{
    // 직접 저장 벡터와 공유 저장 벡터 모두 값 복사 뒤 성분 수정이 분리되어야 한다.
    ExpectFloat("export float Probe(){Vector2 a=Vector2(1,2);Vector2 b=a;b.x=9;return a.x*10+b.x;}",
        19, "vector2 declaration copy", debug);
    ExpectFloat("export float Probe(){Vector3 a=Vector3(1,2,3);Vector3 b;b=a;b.z=9;return a.z*10+b.z;}",
        39, "vector3 assignment copy", debug);
    ExpectFloat("export float Probe(){Vector4 a=Vector4(1,2,3,4);Vector4 b=a;b.w=9;return a.w*10+b.w;}",
        49, "vector4 copy on component write", debug);
    ExpectFloat("export float Probe(){Quaternion a=Quaternion(1,2,3,4);Quaternion b=a;b.w=9;return a.w*10+b.w;}",
        19, "quaternion copy on component write", debug);
    ExpectFloat("float Change(Vector3 value){value.x=9;return value.x;}export float Probe(){Vector3 v=Vector3(1,2,3);return Change(v)*10+v.x;}",
        91, "vector3 argument is by value", debug);
    ExpectFloat("float Change(Vector4 value){value.w=9;return value.w;}export float Probe(){Vector4 v=Vector4(1,2,3,4);return Change(v)*10+v.w;}",
        94, "vector4 argument is by value", debug);
    ExpectFloat("Vector4 source=Vector4(1,2,3,4);Vector4 Read(){return source;}export float Probe(){Vector4 copy=Read();copy.w=9;return source.w*10+copy.w;}",
        49, "vector4 returned value is independent", debug);
    ExpectFloat("Quaternion source=Quaternion(1,2,3,4);Quaternion Read(){return source;}export float Probe(){Quaternion copy=Read();copy.x=9;return source.x*10+copy.x;}",
        29, "returned quaternion preserves nominal type and value", debug);
    ExpectFloat("Vector3 Change(Vector3 value){value.x=9;return value;}export float Probe(){Vector3 v=Vector3(1,2,3);v=Change(v);return v.x;}",
        9, "whole vector value return", debug);
    ExpectFloat("export float Probe(){Vector4 v=Vector4(1,2,3,4);Vector4 copy=v;v=Vector4(5,6,7,8);copy.w+=1;return v.w*10+copy.w;}",
        85, "whole vector copy survives source reassignment", debug);
    ExpectFloat("float Change(Vector3 a,Vector3 b){a=Vector3(5,6,7);b.x+=2;return a.x*10+b.x;}export float Probe(){Vector3 v;return Change(v,v)*10+v.x;}",
        520, "repeated vector arguments remain independent", debug);
    ExpectFloat("float Local(Vector3 value){value.x=9;return value.x;}export float Probe(){Vector3 v=Vector3(1,2,3);return Local(v)*10+v.x;}",
        91, "vector by-value parameter preserves caller", debug);

    ExpectFloat("struct S{Vector3 position;Quaternion rotation;};export float Probe(){S s;return s.position.x+s.position.z+s.rotation.w;}",
        1, "struct vector and quaternion defaults", debug);
    ExpectFloat("struct S{Vector3 position;};export float Probe(){S a=S(Vector3(1,2,3));S b=a;b.position.x=9;return a.position.x*10+b.position.x;}",
        19, "struct vector field copy", debug);
    ExpectFloat("struct I{Vector4 v;};struct O{I inner;};export float Probe(){O a=O(I(Vector4(1,2,3,4)));O b=a;b.inner.v.w+=5;return a.inner.v.w*10+b.inner.v.w;}",
        49, "deep struct vector field copy", debug);
    ExpectFloat("struct S{Vector3 v;};export float Probe(){S s=S(Vector3(1,2,3));Vector3 copy=s.v;s=S(Vector3(5,6,7));copy.x+=2;return s.v.x*10+copy.x;}",
        53, "vector field copy survives whole struct assignment", debug);
    ExpectFloat("struct S{Vector4 v;};S Change(S s,Vector4 v){s=S(Vector4(5,6,7,8));v.w+=2;s.v.w+=v.w;return s;}export float Probe(){S s;s=Change(s,s.v);return s.v.w;}",
        10, "struct and field arguments are separate values", debug);
}

static void CheckOrder(bool debug)
{
    ExpectFloat("Vector3 g=Vector3(1,2,3);int Change(){g.x=9;return 0;}float Read(Vector3 v,int unused){return v.x;}export float Probe(){return Read(g,Change())*10+g.x;}",
        19, "vector3 argument snapshot before later mutation", debug);
    ExpectFloat("Vector4 g=Vector4(1,2,3,4);int Change(){g.w=9;return 0;}float Read(Vector4 v,int unused){return v.w;}export float Probe(){return Read(g,Change())*10+g.w;}",
        49, "vector4 argument snapshot before later mutation", debug);
    ExpectFloat("Vector3 g=Vector3(1,2,3);float Change(){g=Vector3(9,8,7);return 2;}export float Probe(){return g.x+Change();}",
        3, "component value snapshot before later call", debug);
    ExpectFloat("Vector3 g=Vector3(1,2,3);float Change(){g=Vector3(9,8,7);return 2;}export float Probe(){g.x=Change();return g.x*100+g.y*10+g.z;}",
        287, "assignment preserves other components after RHS replacement", debug);
    ExpectFloat("Vector4 g=Vector4(1,2,3,4);float Change(){g=Vector4(9,8,7,6);return 2;}export float Probe(){g.w+=Change();return g.w;}",
        8, "compound reads component after RHS replacement", debug);
    ExpectFloat("float x=1;float Change(){x=9;return 2;}export float Probe(){Vector3 v=Vector3(x,Change(),x);return v.x*100+v.y*10+v.z;}",
        129, "constructor arguments evaluated left to right", debug);
    ExpectFloat("Vector3 g=Vector3(1,2,3);float Change(){g.x=9;return 2;}struct S{Vector3 first;float second;};export float Probe(){S s=S(g,Change());return s.first.x*100+s.second*10+g.x;}",
        129, "struct constructor snapshots vector field argument", debug);
    ExpectFloat("Vector3 g=Vector3(1,2,3);Vector3 Change(){g=Vector3(5,6,7);return g;}export float Probe(){Vector3 copy=(g=Change());copy.x=9;return g.x*10+copy.x;}",
        59, "nested vector assignment result retains value semantics", debug);
}

static void CheckArithmetic(bool debug)
{
    ExpectFloat("export float Probe(){Vector2 a=Vector2(2,4);Vector2 b=Vector2(1,3);Vector2 c=+(a+b)-b;return c.x*10+c.y;}",
        24, "vector2 unary plus and exact-dimension addition subtraction", debug);
    ExpectFloat("export float Probe(){Vector3 a=Vector3(2,4,6);Vector3 b=-(a-Vector3(1,2,3));return b.x*100+b.y*10+b.z;}",
        -123, "vector3 unary minus and subtraction", debug);
    ExpectFloat("export float Probe(){Vector4 a=Vector4(1,2,3,4);Vector4 b=2*a;Vector4 c=b*3.0/2;return c.x*1000+c.y*100+c.z*10+c.w;}",
        3702, "vector4 scalar multiplication in both orders and division", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(1,2,3);v+=Vector3(3,4,5);v-=Vector3(1,2,3);v*=4;v/=2.0;return v.x*100+v.y*10+v.z;}",
        690, "whole-vector compound operators", debug);
    ExpectFloat("export float Probe(){Vector4 a=Vector4(1,2,3,4);Vector4 b=a;b+=Vector4(1,1,1,1);b*=2;return a.w*100+b.w;}",
        410, "heap vector arithmetic preserves copied input", debug);
    ExpectFloat("export float Probe(){Vector4 a=Vector4(1,2,3,4);Vector4 result=(a+=Vector4(1,1,1,1));result.w=9;return a.w*10+result.w;}",
        59, "whole-vector compound expression returns independent value", debug);
    ExpectFloat("Vector3 Scale(Vector3 v){v*=2;return v;}export float Probe(){Vector3 a=Vector3(1,2,3);a=Scale(a);return a.z;}",
        6, "whole-vector arithmetic through value return", debug);
    ExpectFloat("struct S{Vector4 v;};export float Probe(){S a=S(Vector4(1,2,3,4));S b=a;b.v+=Vector4(1,1,1,1);return a.v.w*10+b.v.w;}",
        45, "struct field whole-vector compound preserves value copy", debug);
    ExpectFloat("Vector3 g=Vector3(1,2,3);Vector3 Change(){g=Vector3(9,8,7);return Vector3(2,3,4);}export float Probe(){Vector3 v=g+Change();return v.x*100+v.y*10+g.x;}",
        359, "binary vector arithmetic snapshots left operand", debug);
    ExpectFloat("Vector3 g=Vector3(1,2,3);Vector3 Change(){g=Vector3(9,8,7);return Vector3(2,3,4);}export float Probe(){g+=Change();return g.x*100+g.y*10+g.z;}",
        1221, "whole-vector compound reads target after RHS", debug);
    ExpectFloat("int order=0;int Left(){order=order*10+1;return 2;}Vector2 Right(){order=order*10+2;return Vector2(3,4);}export float Probe(){Vector2 v=Left()*Right();return order*100+v.x*10+v.y;}",
        1268, "scalar-left multiplication preserves operand evaluation order", debug);
    ExpectFloat("int order=0;Vector2 Left(){order=order*10+1;return Vector2(3,4);}int Right(){order=order*10+2;return 2;}export float Probe(){Vector2 v=Left()*Right();return order*100+v.x*10+v.y;}",
        1268, "vector-left multiplication preserves operand evaluation order", debug);
    ExpectFloat("export float Probe(){Vector3 a=Vector3(16777216,0,0);Vector3 b=a+Vector3(1,0,0);return b.x;}",
        16777216, "vector arithmetic retains binary32 components", debug);
    ExpectFloat("Vector3 g=Vector3(1,2,3);int Scalar(){g.x=9;return 2;}export float Probe(){Vector3 value=Scalar()*g;return value.x;}",
        18, "scalar-first multiplication reads vector after scalar side effects", debug);
    ExpectFloat("Vector3 g=Vector3(1,2,3);int Scalar(){g.x=9;return 2;}export float Probe(){Vector3 value=g*Scalar();return value.x;}",
        2, "vector-first multiplication snapshots before scalar side effects", debug);
    ExpectFloat("export float Probe(){Vector2 v=-Vector2(0.0,1.0);return v.x;}",
        -0.0, "unary vector minus preserves negative zero", debug);
    ExpectFloat("export float Probe(){float scalar=1.00000004470348358154296875;Vector3 v=Vector3(3,0,0)*scalar;return v.x;}",
        3, "scalar multiplier narrows before vector arithmetic", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(1,0,-1)/0.0;return v.x;}",
        (std::numeric_limits<double>::infinity)(), "vector division by zero preserves floating infinity", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(1,0,-1)/0.0;return v.y;}",
        (std::numeric_limits<double>::quiet_NaN)(), "zero vector component divided by zero preserves NaN", debug);
}

static void CheckStorage(bool debug)
{
    ExpectFloat("export float Probe(){Vector3 v=Vector3(16777217,0,0);return v.x;}",
        16777216.0, "constructor narrows to binary32", debug);
    ExpectFloat("export float Probe(){Vector3 v;v.y=0.1;return v.y;}",
        static_cast<double>(0.1f), "component writes narrow and reads widen", debug);
    ExpectFloat("export float Probe(){Vector4 v;v.w=16777216;v.w+=1;return v.w;}",
        16777216.0, "compound result stored at binary32 precision", debug);
    ExpectFloat("export float Probe(){Vector3 v;float result=(v.x=16777217);return result;}",
        16777216.0, "component assignment expression returns stored value", debug);
    ExpectFloat("export float Probe(){Vector4 v=Vector4(0,0,0,16777216);float result=(v.w+=1);return result;}",
        16777216.0, "component compound expression returns stored value", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(16777216,0,0);float result=++v.x;return result;}",
        16777216.0, "component prefix expression returns stored value", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(16777216,0,0);float result=v.x++;return result;}",
        16777216.0, "component postfix expression returns prior stored value", debug);
    ExpectFloat("export float Probe(){Vector2 v=Vector2(1.0,0.0);return v.x+0.00000001;}",
        1.00000001, "component scalar arithmetic uses binary64", debug);
    ExpectFloat("export float Probe(){Vector4 v=Vector4(0,0,0,3.4028235e38);return v.w;}",
        static_cast<double>((std::numeric_limits<float>::max)()), "max float32 shortest decimal stays finite", debug);
    ExpectFloat("export float Probe(){Vector3 v;v.x=-3.4028235e38;return v.x;}",
        -static_cast<double>((std::numeric_limits<float>::max)()), "negative max float32 shortest decimal stays finite", debug);
    ExpectFloat("export float Probe(){Vector2 v=Vector2(3.4028235677973366e38,0);return v.x;}",
        (std::numeric_limits<double>::infinity)(), "float32 overflow midpoint rounds to infinity", debug);
    ExpectFloat("export float Probe(){Vector3 v;v.z=-1e300;return v.z;}",
        -(std::numeric_limits<double>::infinity)(), "negative out-of-range component becomes infinity", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(1.000000059604644775390625,0,0);return v.x;}",
        1.0, "float32 halfway rounds to lower even mantissa", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(1.000000178813934326171875,0,0);return v.x;}",
        1.0000002384185791015625, "float32 halfway rounds to upper even mantissa", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(1.401298464324817e-45,0,0);return v.x;}",
        static_cast<double>((std::numeric_limits<float>::denorm_min)()), "minimum float32 subnormal survives construction", debug);
    ExpectFloat("export float Probe(){Vector4 v;v.w=7.006492321624085e-46;return v.w;}",
        0.0, "half minimum float32 subnormal rounds to even zero", debug);
    // 끝자리 086은 binary64 파싱 뒤 정확한 절반값이다. 087부터 다음 double 값으로 올라간다.
    ExpectFloat("export float Probe(){Vector4 v;v.w=7.006492321624087e-46;return v.w;}",
        static_cast<double>((std::numeric_limits<float>::denorm_min)()), "above half minimum subnormal stays nonzero", debug);
    ExpectFloat("export float Probe(){Quaternion q;q.x=-1e-300;return q.x;}",
        -0.0, "negative underflow preserves zero sign", debug);
    ExpectFloat("export float Probe(){Vector3 v=Vector3(-0.0,0,0);return v.x;}",
        -0.0, "constructor preserves negative zero", debug);
    ExpectFloat("export float Probe(){Vector4 v;v.w=-0.0;return v.w;}",
        -0.0, "component write preserves negative zero", debug);
    ExpectFloat("export float Probe(){float zero=0.0;Vector3 v=Vector3(zero/zero,0,0);return v.x;}",
        (std::numeric_limits<double>::quiet_NaN)(), "constructor preserves NaN", debug);
}

static void CheckRejections(bool debug)
{
    struct Invalid { const char* source; const char* label; };
    const Invalid invalid[] = {
        {"Vector2 v=Vector2(1);", "vector2 arity"},
        {"Vector3 v=Vector3(1,2);", "vector3 arity"},
        {"Vector4 v=Vector4(1,2,3,4,5);", "vector4 arity"},
        {"Quaternion q=Quaternion(1,2,3);", "quaternion arity"},
        {"Vector3 v=Vector3(true,0,0);", "bool constructor component"},
        {"Vector3 v=Vector3(\"1\",0,0);", "string constructor component"},
        {"Vector3 v=Vector3(null,0,0);", "null constructor component"},
        {"Vector3 v=Vector2(1,2);", "different vector dimensions"},
        {"Quaternion q=Vector4(1,2,3,4);", "vector4 cannot initialize quaternion"},
        {"Vector4 v=Quaternion(1,2,3,4);", "quaternion cannot initialize vector4"},
        {"void F(Vector4 v){}F(Quaternion());", "quaternion argument mismatch"},
        {"Quaternion F(){return Vector4();}", "quaternion return mismatch"},
        {"Vector3 v;v.x=true;", "bool component write"},
        {"Vector3 v;v.x=null;", "null component write"},
        {"Vector3 v;v.x=Vector3();", "vector component write"},
        {"Vector2 v;float z=v.z;", "missing vector2 z"},
        {"Vector3 v;v.w=1;", "missing vector3 w"},
        {"Vector4 v;float x=v.X;", "component names are case sensitive"},
        {"Quaternion q;float n=q.missing;", "unknown quaternion component"},
        {"Vector3 v;int x=v.x;", "component is float not int"},
        {"Vector3 v;float& x=v.x;", "component cannot bind binary64 reference"},
        {"void F(float& x){}Vector3 v;F(v.x);", "component cannot bind reference argument"},
        {"Vector3& v=Vector3();", "temporary vector reference"},
        {"Vector3().x=1;", "temporary vector component assignment"},
        {"Vector3 F(){return Vector3();}F().x++;", "returned temporary component increment"},
        {"Vector3 v;float x=v[0];", "vector indexing is not exposed"},
        {"Vector3 a;Vector2 b;Vector3 c=a+b;", "mixed vector dimensions in arithmetic"},
        {"Vector3 a;Vector3 b;Vector3 c=a*b;", "componentwise vector multiplication is not implicit"},
        {"Vector3 a;Vector3 b;a/=b;", "vector division requires scalar"},
        {"Vector3 v;v+=2;", "vector addition requires matching vector"},
        {"Vector3 v;v*=true;", "vector scaling rejects bool"},
        {"Vector3 v;Vector3 x=2/v;", "scalar divided by vector is rejected"},
        {"Vector3 a;Vector3 b;bool equal=a==b;", "whole-vector equality remains unsupported"},
        {"Quaternion q;Quaternion r=q+q;", "quaternion addition is not vector addition"},
        {"Quaternion q;q*=2;", "quaternion scaling remains unsupported"},
        {"Quaternion q;Quaternion r=-q;", "quaternion unary arithmetic remains unsupported"},
        {"Quaternion q;Quaternion r=+q;", "quaternion unary plus remains unsupported"},
        {"Vector4 v;Quaternion q;v+=q;", "vector4 cannot consume quaternion in arithmetic"},
        {"Vector3? v=null;float x=v.x;", "nullable vector requires unwrap"},
        {"Vector3? v=Vector3();Vector3 result=v+Vector3();", "nullable vector addition requires unwrap"},
        {"Vector4? v=Vector4();Vector4 result=v*2;", "nullable vector scaling requires unwrap"},
        {"Vector3? v=Vector3();v+=Vector3();", "nullable vector compound requires unwrap"},
        {"Vector3? a=Vector3();Vector3? b=Vector3();bool equal=a==b;", "nullable vector equality deferred"},
        {"Quaternion? a=Quaternion();Quaternion? b=Quaternion();bool different=a!=b;", "nullable quaternion inequality deferred"},
        {"Vector3 v=null;", "nonnull vector rejects null"},
        {"const Vector3 v=Vector3();", "vector const construction deferred"},
        {"float32 x=1.0;", "no float32 scalar declaration"},
        {"int32 x=1;", "no int32 scalar declaration"}
    };
    for (const Invalid& item : invalid)
    {
        NeoCore::Error error;
        const std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(item.source, error, item.label, debug);
        Check(!program && !error.ok(), std::string("reject ") + item.label);
    }
}

static void CheckHostBoundary(bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(
        "int zero=0;int fail=1/zero;export Vector3 Probe(){return Vector3();}",
        error, "scalar host vector result preflight", debug);
    Check(program != nullptr, "vector return source compiles: " + error.message);
    if (program)
    {
        const NeoCore::Result result = program->Call("Probe");
        Check(!result.ok() && result.error.message.find("return type Vector3") != std::string::npos,
            "scalar host vector return rejected before initialization");
        const NeoCore::Result initialized = program->Run();
        Check(!initialized.ok() && initialized.error.message.find("division by zero") != std::string::npos,
            "vector return preflight does not execute global initialization");
    }
}

static void CheckDiagnostics(bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(
        "export float Probe(){\n Vector2 v;\n return v.z;\n}\n", error, "bad_vector_component.nc", debug);
    Check(!program && error.line == 3 && error.sourceName == "bad_vector_component.nc",
        "component diagnostic keeps original source line");
    program = NeoCore::Program::Compile(
        "export float Probe(){\n Vector3 v;\n int zero=0;\n v.x=1/zero;\n return v.x;\n}\n",
        error, "bad_vector_rhs.nc", debug);
    Check(program != nullptr, "vector RHS failure source compiles: " + error.message);
    if (program)
    {
        const NeoCore::Result result = program->Call("Probe");
        Check(!result.ok() && result.error.message.find("division by zero") != std::string::npos &&
            result.error.sourceName == "bad_vector_rhs.nc", "vector RHS retains runtime failure");
        if (debug)
            Check(result.error.line == 4 || result.error.message.find("Line(4)") != std::string::npos,
                "vector RHS retains original line: " + result.error.message);
    }
}

int main()
{
    for (int mode = 0; mode < 2; ++mode)
    {
        const bool debug = mode != 0;
        CheckValues(debug);
        CheckCopiesAndReferences(debug);
        CheckOrder(debug);
        CheckArithmetic(debug);
        CheckStorage(debug);
        CheckRejections(debug);
        CheckHostBoundary(debug);
        CheckDiagnostics(debug);
    }
    std::printf("NeoCore vector regression: %d/%d passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
