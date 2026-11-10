#include "NeoCore.h"
#include <cstdio>
#include <string>

static int checks = 0;
static int failures = 0;

static void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message.c_str()); }
}

static void Expect(const char* source, long long expected, const char* label, bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, label, debug);
    Check(program != nullptr, std::string(label) + " compile: " + error.message);
    if (!program) return;
    NeoCore::Result result = program->Call("Probe");
    Check(result.ok() && !result.suspended(), std::string(label) + " run: " + result.error.message);
    if (!result.ok() || result.suspended()) return;
    Check(result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected, label);
}

static void Reject(const char* source, const char* label)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, label);
    Check(!program && !error.message.empty(), std::string(label) + " must reject");
}

// 컴파일은 통과하지만 실행이 지정한 메시지로 실패해야 하는 경우.
static void FailsAtRuntime(const char* source, const char* messagePart, const char* label, bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, label, debug);
    Check(program != nullptr, std::string(label) + " compile: " + error.message);
    if (!program) return;
    NeoCore::Result result = program->Call("Probe");
    Check(!result.ok() && result.error.message.find(messagePart) != std::string::npos,
        std::string(label) + " runtime failure: " + result.error.message);
}

int main()
{
    for (int mode = 0; mode != 2; ++mode)
    {
        const bool debug = mode != 0;
        Expect("int Add(int a,int b){return a+b;}export int Probe(){fn(int,int)->int f=Add;return f(2,3);}", 5, "named function value", debug);
        Expect("int Apply(fn(int)->int f,int x){return f(x);}export int Probe(){return Apply(fn(int x)->int{return x*3;},7);}", 21, "higher order function", debug);
        Expect("fn()->int Make(int value){return fn()->int{value+=1;return value;};}export int Probe(){fn()->int a=Make(3);fn()->int b=Make(10);return a()*100+a()*10+b();}", 461, "independent persistent captures", debug);
        Expect("export int Probe(){int x=3;fn()->int f=fn()->int{return x;};x=9;return f()*10+x;}", 39, "capture snapshot", debug);
        Expect("fn()->fn()->int Make(int value){return fn()->fn()->int{return fn()->int{value+=1;return value;};};}export int Probe(){fn()->int f=Make(5)();return f()*10+f();}", 67, "nested capture lifetime", debug);
        Expect("struct Inner{int x;};struct Outer{Inner value;};export int Probe(){Outer original=Outer(Inner(3));fn()->int f=fn()->int{original.value.x+=1;return original.value.x;};original.value.x=9;return f()*100+f()*10+original.value.x;}", 459, "nested struct capture copies", debug);
        Expect("export int Probe(){Vector3 v=Vector3(1,2,3);fn()->float f=fn()->float{v.x+=1;return v.x;};v.x=8;return int(f()+f()+v.x);}", 13, "boxed vector capture copies", debug);
        Expect("struct S{int x;};export int Probe(){S original=S(3);fn(S)->S f=fn(S item)->S{item.x+=4;return item;};S result=f(original);return original.x*10+result.x;}", 37, "indirect struct argument return copies", debug);
        // return 뒤의 도달하지 못하는 문장은 그대로 명령이 되지만 함수는 여전히 종결 명령으로 끝나야 한다.
        Expect("void F(){return;print(1);}export int Probe(){F();return 7;}", 7, "unreachable statement after void return", debug);
        Expect("int G(){return 1;print(1);}export int Probe(){return G()+1;}", 2, "unreachable statement after value return", debug);
        Expect("int H(int x){if(x>0){return 1;print(1);}else{return 2;print(2);}}export int Probe(){return H(1)*10+H(-1);}", 12, "unreachable statements in both branches", debug);
        Expect("int next=0;string Next(){next+=1;return tostring(next);}export int Probe(){"
            "string text=(Next() .. Next()) .. (Next() .. Next()) .. Next();if(text!=\"12345\")return -1;return next;}",
            5, "iterative concatenation keeps grouping and call order", debug);
        // 루프 안에서 쓰인 구조체 필드 키 상수는 숨은 지역 슬롯으로 옮겨진다. 루프 밖의 같은 필드 읽기도 필드 타입을 유지해야 한다.
        Expect("struct Inner{list<int> state;list<int> order;};struct Outer{int a;int b;Inner bld;};"
            "int Apply(Outer p){int sum=0;list<int> s=p.bld.state;for(int i in 0,p.bld.order.len())sum+=p.bld.order[i];return sum*100+s[0];}"
            "export int Probe(){Outer p=Outer(1,2,Inner([5,6],[7,8]));return Apply(p);}", 1505, "hoisted struct field key keeps the field type", debug);
        // 오른쪽이 16비트 정수 상수인 정수 산술·비교 점프는 로드 시 즉값 형식이 된다. 넘침·부호·반전 의미가 같아야 한다.
        Expect("int Add1(int x){return x+1;}int Sub2(int x){return x-2;}int Mul3(int x){return x*-3;}int Mask(int x){return x&-16;}"
            "int Or5(int x){return x|5;}int Xor7(int x){return x^7;}int Shl3(int x){return x<<3;}int Shr2(int x){return x>>2;}"
            "export int Probe(){int big=9223372036854775807;int r=0;if(Add1(big)==-big-1)r+=1;if(Sub2(1)==-1)r+=2;if(Mul3(5)==-15)r+=4;"
            "if(Mask(37)==32)r+=8;if(Or5(8)==13)r+=16;if(Xor7(5)==2)r+=32;if(Shl3(3)==24)r+=64;if(Shr2(40)==10)r+=128;return r;}",
            255, "immediate integer arithmetic keeps wraparound and sign", debug);
        Expect("int Lt(int x){if(x<-2)return 1;return 0;}int Ge(int x){if(!(x<5))return 1;return 0;}int Eq(int x){if(x==32767)return 1;return 0;}"
            "int Big(int x){if(x<40000)return 1;return 0;}"
            "export int Probe(){return Lt(-3)*1000+Lt(-2)*100+Ge(5)*10+Ge(4)+Eq(32767)*10000+Big(39999)*100000;}",
            111010, "immediate integer comparison jumps keep inversion and range", debug);
        Expect("export int Probe(){int s=0;for(int i in 0,1000,1)s+=(i&255)+1;return s;}", 125716, "loop integer constants stay immediate", debug);
        // 로드가 인접 명령 쌍(MOVI·MOVI, ADD·ADD2, MOVI·JMP→JFOR)을 한 디스패치로 묶어도, 둘째 명령으로 들어오는
        // 점프(건너뛴 if 뒤의 대입)는 둘째만 실행해야 하고 continue 는 반복 점검으로 바로 가야 한다.
        Expect("export int Probe(){int x=0;int y=0;int s=0;for(int i in 0,5){if((i&1)==0)x=1;y=2;s+=x+y;x=0;}return s;}", 13, "paired instructions keep incoming jump targets", debug);
        Expect("export int Probe(){int s=0;for(int i in 0,10){if(i==3)continue;s+=i;}return s;}", 42, "loop entry test in place keeps continue", debug);
        // 순수 수학 네이티브는 창 슬롯에서 바로 계산한다(NeoLib kScalarNatives). 인자 1·2·3개, 정수 인자, fmod 의 0 보호를 함께 본다.
        Expect("import math;export int Probe(){float h=math.hypot(3,4);float c=math.Clamp(5,0,3);float f=math.fmod(1,0);float m=math.Min(2,1.5);"
            "float l=math.Lerp(0,10,0.25);float s=math.sqrt(16);float a=math.abs(-2.5);return int(h*1000)+int(c*100)+int(f)+int(m*10)+int(l*10)+int(s)+int(a*2);}",
            5349, "scalar natives compute in place", debug);
        // if/while 조건의 중첩 논리식(`a||b||c`, `a&&b||c`, `a&&(b||c)`)은 불린 값을 만들지 않고 분기 점프로만 컴파일된다.
        // 진리표와 단락 평가(함수 호출 횟수)가 그대로여야 한다.
        Expect("int K(int a,int b){if(a==1||a<0||a>=10)return 1;if(a<3&&b<3||a>8)return 2;if(a<3&&(b<3||b>8))return 3;return 0;}"
            "export int Probe(){return K(1,9)*1000+K(5,1)*100+K(9,9)*10+K(2,9);}", 1023, "nested condition groups branch directly", debug);
        Expect("int calls=0;bool T(bool v){calls+=1;return v;}int W(int a){int n=0;while(a<5||a==7){a+=1;n+=1;}return n;}"
            "export int Probe(){int r=0;if(T(true)||T(false)||T(true))r+=1;if(T(false)&&T(true)||T(true))r+=10;return r*1000+calls*100+W(0)*10+W(7)+W(6);}",
            11351, "nested condition groups keep short-circuit order", debug);
        // 호출 인자 중 뒤쪽에 호출이 없는 것은 임시 없이 창 슬롯에 바로 계산된다. 값, 왼쪽부터의 평가 순서, 중첩 호출과
        // 섞인 자리, 소유 인자(PREPARE_CALL) 뒤의 직접 계산을 함께 본다.
        Expect("int F(int a,int b){return a*10+b;}int H(int y){return y+1;}list<int> arr=[1,2,3];int c=0;int Next(){c+=1;return c;}"
            "int G3(int a,int b,int d){return a*100+b*10+d;}export int Probe(){int i=1;int x=5;int r=F(x+1,x*2);r=r*100+F(arr[i],x);"
            "r=r*100+F(x,H(x));r=r*100+F(H(x),x);r=r*1000+G3(x-4,Next(),x+Next()*10);return r;}",
            70255665135LL, "arguments computed in the call window", debug);
        Expect("int S(string s,int n){return s.len()+n;}float G(float a,float b){return a*b;}export int Probe(){string s=\"ab\";int n=2;"
            "float c=-3;return S(s..\"a\",n+1)*100+int(G(1.0+2.0,c>0?c:-c));}", 609, "owned and conditional arguments keep staging order", debug);
        // 네이티브·선택자 호출(레거시 경로)도 같은 규칙으로 인자를 창에 바로 만든다. 호출이 섞인 인자(F(1), Next())는 임시로 남는다.
        Expect("import math;float Dist(float x0,float y0,float x1,float y1){return math.hypot(x1-x0,y1-y0);}int F(int a){return a*2;}"
            "int c=0;int Next(){c+=1;return c;}export int Probe(){list<int> lst=[];string s=\"abcdef\";float x=3;float y=4;int n=2;"
            "int r=int(Dist(0,0,x,y)*100);r=r*10+int(math.Clamp(x*2,1,n+3));r=r*10+int(math.Max(F(1),x+2));r=r*10+int(math.Min(x+Next(),F(Next())));"
            "lst.append(n*3);lst.append(F(n)+1);lst.append(lst[0]+lst[1]);r=r*100000+lst[0]*10000+lst[1]*100+lst[2];return r*100+s.find(\"c\")+s.len()*10;}",
            5005546051162LL, "native and selector arguments computed in the call window", debug);
        // 이름 붙은 숫자·불린 상수는 타입 IR의 리터럴이다. 색인 산술에 상수가 끼어도 앞서 읽은 지역 변수와 전역 배열을
        // 복사하지 않고, 값과 평가 순서(상수 뒤의 대입·호출)는 그대로다.
        Expect("const int K=8;const float H=0.5;const bool ON=true;array<int> g=array<int>(64,0);int c=0;int Next(){c+=1;return c;}"
            "export int Probe(){int s=0;for(int f in 0,4){for(int r in 0,K)g[f*K+r]=f+r;}for(int i in 0,32)s+=g[i];float x=3;"
            "if(ON&&x*H>1.0)s+=1000;int i=2;int v=i*K+(i=5);int w=i*K+Next();return s*10000+v*100+w;}",
            11602141, "named constants are literals in typed expressions", debug);
        // 범위 점프 합치기(`x < 0 || x > 10`)는 둘째 비교로 들어오는 점프가 없을 때만 한다. `a && x < 0 || x > 10` 에서
        // a 가 거짓이면 둘째 비교만 봐야 한다(합치면 몸통으로 떨어졌다).
        Expect("int K(bool a,int x){if(a&&x<0||x>10)return 1;return 0;}int M(bool a,int dx,int dy){if(a&&dx>0||dy<0)return 1;return 0;}"
            "int P(bool a,int x){if((a&&x<0)||x>10)return 1;return 0;}int Q(int x){if(x<0||x>10)return 1;return 0;}"
            "export int Probe(){return K(false,5)*10000000+M(false,3,3)*1000000+P(false,5)*100000+K(true,-1)*10000+K(false,11)*1000+K(true,5)*100+Q(-1)*10+Q(5);}",
            11010, "range jump fusion leaves a targeted second comparison alone", debug);
        // 즉값으로 창에 놓은 리터럴은 상수 풀에서 되돌린다. 같은 리터럴을 아직 방출되지 않은 바깥 피연산자가 들고
        // 있으면 되돌리지 않는다(되돌리면 바깥 피연산자가 다음 상수를 읽었다).
        Expect("int A1(int a){return a*3;}int A2(int a,int b){return a*3+b;}int A3(int a,int b,int c){return a+b*2+c*5;}int R(){return 41+A2(41,77);}"
            "export int Probe(){int x=1;if(43<A2(43,79))x=5;int y=A2(45,A2(45,81));"
            "return (7+A3(7,255,0))*1000000000+R()*1000000+x*100000+(9+A1(9))*1000+y;}",
            524241536351LL, "immediate arguments keep a literal shared with a pending operand", debug);
        // while 조건은 본문 뒤로 옮겨 쓴다. 조건 안의 직접 호출(PREPARE_CALL 없음) 계획도 함께 옮겨야
        // 본문의 문자열 인자 호출 계획이 조건의 float 인자 복사에 쓰이지 않는다.
        Expect("float Gap(float a,float b){return a-b;}int notes=0;void Note(string s){notes+=s.len();}"
            "export int Probe(){int t=0;float x=10.0;while(t<20&&Gap(x,1.0)>2.0){Note(\"fly\");x-=1.0;t++;}return t*100+notes;}",
            721, "relocated while condition keeps its own call plan", debug);
        // `x += c[k];` 문장은 읽기와 제자리 덧셈을 합친 명령 하나가 된다. 결과·오류·감싸기는 두 명령일 때와 같아야 한다.
        Expect("int total=0;export int Probe(){map<string,int> m={\"a\":1,\"b\":2};list<int> l=[3,4];map<int,int> mi={7:5};int s=0;"
            "for(int i in 0,3){s+=m[\"a\"];s+=m[\"b\"];s+=l[1];s+=mi[7];total+=m[\"b\"];}return s*10+total;}",
            366, "fused container load add into local and global", debug);
        Expect("export int Probe(){map<string,float> m={\"a\":1.5};list<float> l=[2.5];map<int,float> mi={1:0.25};float f=0.0;"
            "for(int i in 0,4){f+=m[\"a\"];f+=l[0];f+=mi[1];}return int(f*100);}",
            1700, "fused float container load add", debug);
        Expect("export int Probe(){map<string,int> m={\"a\":1};int s=9223372036854775807;s+=m[\"a\"];if(s<0)return 1;return 0;}",
            1, "fused load add wraps like the in-place add", debug);
        Expect("export int Probe(){map<string,int> m={\"a\":2};int s=3;int t=(s+=m[\"a\"]);return s*10+t;}", 55, "used compound result stays a plain load and add", debug);
        Expect("export int Probe(){map<string,int> m={\"a\":2};float f=0.5;f+=m[\"a\"];return int(f*10);}", 25, "widened element keeps conversion before the add", debug);
        FailsAtRuntime("export int Probe(){map<string,int> m={\"a\":1};int s=0;s+=m[\"zz\"];return s;}", "map key not found", "fused load add reports a missing map key", debug);
        FailsAtRuntime("export int Probe(){list<int> l=[1];int s=0;s+=l[3];return s;}", "out of range", "fused load add reports a list index error", debug);
        FailsAtRuntime("export int Probe(){map<string,int> m;int s=0;s+=m[\"a\"];return s;}", "null map", "fused load add reports a null map", debug);
        // 참조 하나뿐인 문자열에 .. 결과를 쓰면 버퍼를 다시 쓴다. 별칭·인자·캡처·상수가 보는 값은 바뀌지 않아야 한다.
        Expect("export int Probe(){string s=\"\";for(int i in 0,1000)s=s..\"x\";return s.len();}", 1000, "in-place string accumulation", debug);
        Expect("export int Probe(){string s=\"ab\";string t=s;s=s..\"c\";return t.len()*10+s.len();}", 23, "aliased string keeps its value when the other is appended", debug);
        Expect("void F(string a){a=a..\"!\";}export int Probe(){string s=\"hi\";F(s);if(s!=\"hi\")return 0;return s.len();}", 2, "argument copy is not appended in place", debug);
        Expect("export int Probe(){string s=\"a\";fn()->string f=fn()->string{return s;};s=s..\"b\";if(f()!=\"a\")return 0;return s.len();}", 2, "capture keeps the value before the append", debug);
        Expect("export int Probe(){string s=\"k\";s=s..\"x\";string c=\"k\";if(s!=\"kx\"||c!=\"k\")return 0;return s.len();}", 2, "constant string is not rewritten", debug);
        Expect("export int Probe(){string s=\"ab\";s=s..s;s=s..s;if(s!=\"abababab\")return 0;s=\"<\"..s;if(s!=\"<abababab\")return 0;return s.len();}", 9, "self and prepend concatenation", debug);
        Expect("export int Probe(){string s=\"\";for(int i in 0,3)s=s..\"a\"..tostring(i)..\"b\";if(s!=\"a0ba1ba2b\")return 0;return s.len();}", 9, "chain into target keeps order and content", debug);
        Expect("export int Probe(){string s=\"ab\";s=s..\"-\"..s;if(s!=\"ab-ab\")return 0;return s.len();}", 5, "chain whose right side is the target stays separate", debug);
        Expect(u8"export int Probe(){string s=\"가\";for(int i in 0,3)s=s..\"나\"..tostring(i);string t=\"é\";t=t..\"x\";return s.len()*10+t.len();}", 72, "character counts after appends", debug);
        Expect("export int Probe(){string s=\"hp \"..tostring(12)..\" / 100\";if(s!=\"hp 12 / 100\")return 0;return s.len();}", 11, "literal-led chain builds the full text", debug);
        // 긴 .. 사슬은 항마다 조각 파싱을 중첩하지 않아야 파서 깊이 한도에 걸리지 않는다.
        {
            std::string chain = "export int Probe(){string s=\"x\"";
            for (int i = 0; i < 199; ++i) chain += " .. \"x\"";
            chain += ";return s.len();}";
            Expect(chain.c_str(), 200, "long string concatenation chain", debug);
            std::string calls = "export int Probe(){int v=7;string s=tostring(v)";
            for (int i = 0; i < 99; ++i) calls += " .. tostring(v)";
            calls += ";return s.len();}";
            Expect(calls.c_str(), 100, "long concatenation chain of calls", debug);
        }
        Expect("int F(int x){return x+1;}export int Probe(){map<string,fn(int)->int> m={\"x\":F};return m[\"x\"](7);}", 8, "function map and result call", debug);
        Expect("struct Box{fn(int)->int callback;};export int Probe(){Box box=Box(fn(int x)->int{return x*2;});return box.callback(7);}", 14, "function-valued struct field", debug);
        Expect("int Twice(int x){return x*2;}int Apply(fn(int)->int f,int v){return f(v);}export int Probe(){return Apply(Twice,4);}", 8, "named function as call argument", debug);
        Expect("int Add(int a,int b){return a+b;}int Apply(fn(int,int)->int f,int v){return f(v,1);}export int Probe(){return Apply(Add,1+2)*10+Apply(Add,5);}", 46, "named function argument mixed with typed arguments", debug);
        Expect("int Three(){return 3;}fn()->int Pick(){return Three;}export int Probe(){return Pick()();}", 3, "named function returned as a value", debug);
        Expect("int Base(){return 7;}fn()->int Choose(bool first,fn()->int other){if(first)return Base;return other;}export int Probe(){return Choose(true,fn()->int{return 1;})()+Choose(false,fn()->int{return 1;})();}", 8, "named function and lambda on different return paths", debug);
        Expect("public struct S{int x;};export int Probe(){S s=S(5);return s.x;}", 5, "public struct accepted at file scope", debug);
        Reject("export struct S{int x;};export int Probe(){return 1;}", "export struct rejected");
        Reject("const struct S{int x;};export int Probe(){return 1;}", "const struct rejected");
        Reject("export int Probe(){public struct S{int x;};return 1;}", "public struct inside a function rejected");
        Expect("export int Probe(){list<list<int>> m=[[1,2],[3]];m.append([4,5]);int sum=0;foreach(list<int> row in m)foreach(int x in row)sum+=x;return sum;}", 15, "nested lists and foreach", debug);
        Expect("export int Probe(){map<string,list<int>> m={\"a\":[1,2]};list<int> a=m[\"a\"];if(a!=null)return a[1];return 0;}", 2, "collection map read without a nullable type", debug);
        Expect("export int Probe(){map<string,list<int>> m={};if(!m.contains(\"missing\"))return 7;return m[\"missing\"][0];}", 7, "contains guards a missing collection map key", debug);
        Expect("export int Probe(){list<int> a=[1,2,3];int total=0;if(a!=null)foreach(int value in a)total+=value;return total;}", 6, "null comparison on a collection", debug);
        Expect(u8"export int Probe(){int count=0;foreach(string c in \"A한글\"){count+=c.len();}return count;}", 3, "foreach characters are strings", debug);
        Expect("struct S{int x;};export int Probe(){list<S> values=[S(3)];foreach(S item in values)item.x=7;S original=values[0];return original.x;}", 3, "foreach struct values copy", debug);
        Expect("struct S{int x;};export int Probe(){list<S> a=[S(3)];list<S> b=a.clone();b[0].x=9;list<S> c=a+b;c[0].x=8;return a[0].x*100+b[0].x*10+c[0].x;}", 398, "struct list clone and concatenation copy", debug);
        Expect("struct S{int x;};export int Probe(){list<S> a=[];S fill=S(2);a.resize(2,fill);a[0].x=9;return fill.x*100+a[0].x*10+a[1].x;}", 292, "struct resize fill copies each element", debug);
        Expect("struct S{int x;};export int Probe(){map<string,S> a={\"x\":S(3)};map<string,S> b=a.clone();list<S> c=a.values();c[0].x=9;S x=b[\"x\"];return x.x*10+c[0].x;}", 39, "struct map materialization copies", debug);
        Expect("import system;export int Probe(){set<int> a=set<int>([1,2]);set<int> b=set<int>([2,3]);set<int> c=a|b;set<int> d=a&b;set<int> e=a-b;return c.len()*100+d.len()*10+e.len();}", 311, "set algebra and typed constructors", debug);
        Expect("import system;export int Probe(){set<string> names=set<string>([\"a\",\"b\",\"a\"]);names.add(\"c\");return names.len();}", 3, "string set elements", debug);
        Reject("import system;export int Probe(){set<float> s=set<float>();return s.len();}", "float set elements rejected");
        Reject("import system;export int Probe(){set<bool> s=set<bool>();return s.len();}", "bool set elements rejected");
        Reject("import system;export int Probe(){list<float> values=[1.0];set<float> s=system.set(values);return s.len();}", "system.set with float list rejected");
        Reject("struct S{int id;};import system;export int Probe(){set<S> s=set<S>();return s.len();}", "struct set elements rejected");
        Expect("export int Probe(){list<int> a=[3,1,2];a.sort(fn(int x,int y)->bool{return x<y;});return a[0]*100+a[1]*10+a[2];}", 123, "typed sort comparator", debug);
        // 비교자 안의 정렬이 끝나도 바깥 비교자와 호출자의 실행 위치를 각각 복원해야 한다.
        Expect("bool Less(int a,int b){return a<b;}bool Outer(int a,int b){list<int> inner=[2,1];inner.sort(Less);return a<b;}"
            "export int Probe(){list<int> a=[3,1,2];a.sort(Outer);return a[0]*100+a[1]*10+a[2];}",
            123, "nested named sort comparators restore each parent", debug);
        Expect("export int Probe(){int direction=1;list<int> a=[3,1,2];a.sort(fn(int x,int y)->bool{"
            "list<int> b=[3,1,2];b.sort(fn(int p,int q)->bool{return p*direction<q*direction;});return x*direction<y*direction;});"
            "return a[0]*100+a[1]*10+a[2]+direction*1000;}",
            1123, "nested captured sort comparators preserve captures and caller", debug);
        FailsAtRuntime("bool Fail(int a,int b){int zero=0;return a/zero<b;}bool Outer(int a,int b){list<int> inner=[2,1];inner.sort(Fail);return a<b;}"
            "export int Probe(){list<int> a=[3,1,2];a.sort(Outer);return 123;}",
            "division by zero", "nested sort keeps the failed comparator error trap", debug);
        Expect("import coroutine;int total=0;void Task(int x){total+=x;yield;total+=2;yield;total+=3;}export int Probe(){coroutine<fn(int)->void> c=coroutine.create(Task);coroutine.resume(c,4);int first=total;coroutine.resume(c,4);coroutine.resume(c,4);return first*10+total;}", 49, "typed coroutine suspension", debug);
        Expect("import system;export int Probe(){TimeSpan a=system.time_span_ms(5);TimeSpan b=a.add(system.time_span_ms(7));return b.total_milliseconds();}", 12, "opaque builtin methods", debug);
        Expect("const int Idle=0;const int Moving=Idle+1;int Pick(int state){switch(state){case Idle:return 1;case Moving,2:return 2;case -1:return 3;default:return 9;}}export int Probe(){return Pick(0)*1000+Pick(2)*100+Pick(-1)*10+Pick(7);}", 1239, "int switch with const labels and default", debug);
        Expect("int Pick(int kind){int result=0;switch(kind){case 1:result=10;default:;}return result;}export int Probe(){return Pick(1)*100+Pick(7);}", 1000, "empty statement as a switch default body", debug);
        Expect("int Pick(int kind){switch(kind){case 1:;case 2:return 2;default:return 0;}return 9;}export int Probe(){return Pick(1)*100+Pick(2)*10+Pick(3);}", 920, "empty statement as a case body does not fall through", debug);
        Expect("void Nothing(){;}export int Probe(){Nothing();int x=1;if(x==1);return x+1;}", 2, "empty statements in blocks and if bodies", debug);
        Expect("int Code(string command){int code=0;switch(command){case \"start\",\"resume\":code=1;break;case \"stop\":code=2;break;default:code=9;}return code;}export int Probe(){return Code(\"resume\")*100+Code(\"stop\")*10+Code(\"x\");}", 129, "string switch dispatch", debug);
        Expect("export int Probe(){int total=0;for(int i in 0,6){switch(i%3){case 0:continue;case 1:total+=10;break;default:int local=i*100;total+=local;}total+=1;}return total;}", 724, "switch break and continue inside a loop", debug);
        Expect("export int Probe(){int x=5;int seen=0;switch(x){case 1:seen=1;break;case 2:seen=2;break;}return seen;}", 0, "unmatched switch without default skips", debug);
        // 참조형은 선언만 하면 null 이고, null 비교는 어디서나 가능하며, 가드 없는 접근은 런타임에 실패한다.
        Expect("export int Probe(){string s;list<int> a;map<string,int> m;fn()->int f;int n=0;if(s==null)n+=1;if(a==null)n+=10;if(m==null)n+=100;if(f==null)n+=1000;return n;}", 1111, "reference declarations default to null", debug);
        Expect("string Maybe(bool present){if(present)return \"v\";return null;}export int Probe(){string s=Maybe(true);string t=Maybe(false);int total=0;if(s!=null)total+=s.len();if(t==null)total+=10;return total;}", 11, "string results carry null", debug);
        Expect("export int Probe(){list<int> a=[1,2];int total=0;int i=0;while(i<2){total+=a[i];i+=1;}foreach(int v in a)total+=v;a=null;if(a==null)total+=100;return total;}", 106, "collections accept null after use", debug);
        Expect("export int Probe(){string a=\"ab\";string b=null;if(a!=null&&b!=null)return 1;if(a==null||b==null)return 2;return 3;}", 2, "null comparisons combine with logical operators", debug);
        Expect("struct S{int x;string name;list<int> values;};export int Probe(){S s;if(s.name==null&&s.values==null&&s.x==0)return 1;return 0;}", 1, "struct reference fields default to null", debug);
        FailsAtRuntime("export int Probe(){string s;return s.len();}", "len", "method call on a null string fails at runtime", debug);
        FailsAtRuntime("export int Probe(){list<int> a;return a[0];}", "null", "index read on a null list fails at runtime", debug);
        FailsAtRuntime("export int Probe(){fn()->int f;return f();}", "null", "calling a null function value fails at runtime", debug);
        FailsAtRuntime("export int Probe(){map<string,int> m={\"a\":1};return m[\"b\"];}", "map key not found", "missing key read on a value map fails at runtime", debug);
        FailsAtRuntime("void __neocore_fail(string message){}export int Probe(){map<string,int> m={};return m[\"missing\"];}",
            "map key not found", "a user function cannot intercept an internal failure", debug);
        FailsAtRuntime("int __neocore_fail=7;export int Probe(){map<string,int> m={};return m[\"missing\"];}",
            "map key not found", "a user variable cannot hide an internal failure", debug);
        Expect("int count=0;void __neocore_fail(string text){count+=1;}export int Probe(){__neocore_fail(\"user\");return count;}",
            1, "double underscore names remain valid user declarations", debug);
        Expect("export int Probe(){map<string,int> m={\"a\":1};m[\"b\"]=2;int c=100;if(m.contains(\"c\"))c=m[\"c\"];return m[\"a\"]*10+m[\"b\"]+c;}", 112, "value map reads writes and explicit contains guard", debug);
    }
    Reject("string? s=null;", "nullable marker on a reference type");
    Reject("int? x=null;", "nullable marker on a value type");
    Reject("int x=null;", "null into an int");
    Reject("Vector3 v=null;", "null into a vector");
    Reject("struct S{int x;};S s=null;", "null into a struct");
    Reject("int x=1;bool b=x==null;", "int compared with null");
    Reject("float f=1.0;bool b=null!=f;", "float compared with null");
    Reject("Vector3 v=Vector3();bool b=v==null;", "vector compared with null");
    Reject("struct S{int x;};S s;bool b=s!=null;", "struct compared with null");
    Reject("list<int?> a=[];", "nullable list element");
    Reject("map<string,int?> m={};", "nullable map value");
    Reject("map<string,int> m={};m[\"a\"]+=1;", "compound assignment on a map element");
    Reject("map<string,int> m={};m[\"a\"]++;", "increment on a map element");
    Reject("float f=1.0;switch(f){case 1:print(1);}", "float switch selector");
    Reject("bool b=true;switch(b){case true:print(1);}", "bool switch selector");
    Reject("string s=\"a\";switch(s){case 1:print(1);}", "int label on string selector");
    Reject("int x=1;switch(x){case 1:print(1);case 1:print(2);}", "duplicate case literal");
    Reject("const int A=2;int x=1;switch(x){case A:print(1);case 1+1:print(2);}", "duplicate case through constants");
    Reject("int x=1;switch(x){case \"a\":print(1);}", "case label type mismatch");
    Reject("int x=1;switch(x){case 1:print(1);default:print(2);default:print(3);}", "duplicate default");
    Reject("int x=1;switch(x){case 1:case 2:print(1);}", "empty switch clause");
    Reject("int x=1;int y=2;switch(x){case y:print(1);}", "non-constant case label");
    Reject("string s=null;switch(s){case 1:print(1);}", "switch label type must match a string selector");
    Reject("case 1:print(1);", "case outside switch");
    Reject("int F(int x){switch(x){case 1:return 1;default:break;}}", "switch break path must still return");
    Reject("int x=1;switch(x){case 1:continue;}", "continue needs a loop even inside switch");
    Reject("int x=1;switch(x){case 1:int local=1;}print(local);", "switch clause scope");
    Reject("int F(int x){return x;}fn(string)->int f=F;", "incompatible function assignment");
    Reject("int F(int x){return x;}fn(int)->int f=F;f(\"wrong\");", "indirect argument type");
    Reject("fn()->int f=fn()->int{};", "missing lambda return");
    Reject("fn()->int f=fn()->int{return \"wrong\";};", "lambda return type");
    Reject("list<int> a=[];a.sort(fn(string a,string b)->bool{return a<b;});", "sort comparator type");
    Reject("import system;TimeSpan a=system.time_span_ms(1);a.add(system.stopwatch());", "opaque object mismatch");
    Reject("TimeSpan a=null;print(a);", "opaque values cannot be printed");
    Reject("list<int> a=[];a.append(\"wrong\");", "list append element type");
    Reject("\"abc\".regex_match(1);", "regex argument type");
    Reject("import coroutine;void F(int x){}coroutine<fn(int)->void> c=coroutine.create(F);coroutine.resume(c,\"wrong\");", "coroutine argument type");
    Reject("import coroutine;int F(){return 1;}coroutine.create(F);", "coroutine return signature");
    Reject("import system;AsyncRequest a=system.async_create();a.get(1,\"url\",fn(int x,string s)->void{});", "http callback signature");
    Reject("int F(){return later;}fn()->int f=F;int later=1;", "function reference initialization order");
    std::printf("NeoCore language extensions: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
