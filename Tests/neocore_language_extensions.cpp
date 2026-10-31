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
        Expect("int F(int x){return x+1;}export int Probe(){map<string,fn(int)->int> m={\"x\":F};return m.get(\"x\",F)(7);}", 8, "function map and result call", debug);
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
        Expect("export int Probe(){map<string,list<int>> m={\"a\":[1,2]};list<int>? a=m[\"a\"];if(a!=null)return a[1];return 0;}", 2, "nullable collection refinement", debug);
        Expect("export int Probe(){list<int>? a=[1,2,3];int total=0;if(a!=null)foreach(int value in a)total+=value;return total;}", 6, "guarded nullable foreach", debug);
        Expect(u8"export int Probe(){int count=0;foreach(string c in \"A한글\"){count+=c.len();}return count;}", 3, "foreach characters are strings", debug);
        Expect("struct S{int x;};export int Probe(){list<S> values=[S(3)];foreach(S item in values)item.x=7;S original=values[0];return original.x;}", 3, "foreach struct values copy", debug);
        Expect("struct S{int x;};export int Probe(){list<S> a=[S(3)];list<S> b=a.clone();b[0].x=9;list<S> c=a+b;c[0].x=8;return a[0].x*100+b[0].x*10+c[0].x;}", 398, "struct list clone and concatenation copy", debug);
        Expect("struct S{int x;};export int Probe(){list<S> a=[];S fill=S(2);a.resize(2,fill);a[0].x=9;return fill.x*100+a[0].x*10+a[1].x;}", 292, "struct resize fill copies each element", debug);
        Expect("struct S{int x;};export int Probe(){map<string,S> a={\"x\":S(3)};map<string,S> b=a.clone();list<S> c=a.values();c[0].x=9;S x=b.get(\"x\",S(0));return x.x*10+c[0].x;}", 39, "struct map materialization copies", debug);
        Expect("import system;export int Probe(){set<int> a=set<int>([1,2]);set<int> b=set<int>([2,3]);set<int> c=a|b;set<int> d=a&b;set<int> e=a-b;return c.len()*100+d.len()*10+e.len();}", 311, "set algebra and typed constructors", debug);
        Expect("import system;export int Probe(){set<string> names=set<string>([\"a\",\"b\",\"a\"]);names.add(\"c\");return names.len();}", 3, "string set elements", debug);
        Reject("import system;export int Probe(){set<float> s=set<float>();return s.len();}", "float set elements rejected");
        Reject("import system;export int Probe(){set<bool> s=set<bool>();return s.len();}", "bool set elements rejected");
        Reject("import system;export int Probe(){list<float> values=[1.0];set<float> s=system.set(values);return s.len();}", "system.set with float list rejected");
        Reject("struct S{int id;};import system;export int Probe(){set<S> s=set<S>();return s.len();}", "struct set elements rejected");
        Expect("export int Probe(){list<int> a=[3,1,2];a.sort(fn(int x,int y)->bool{return x<y;});return a[0]*100+a[1]*10+a[2];}", 123, "typed sort comparator", debug);
        Expect("import coroutine;int total=0;void Task(int x){total+=x;yield;total+=2;yield;total+=3;}export int Probe(){coroutine<fn(int)->void> c=coroutine.create(Task);coroutine.resume(c,4);int first=total;coroutine.resume(c,4);coroutine.resume(c,4);return first*10+total;}", 49, "typed coroutine suspension", debug);
        Expect("import system;export int Probe(){TimeSpan a=system.time_span_ms(5);TimeSpan b=a.add(system.time_span_ms(7));return b.total_milliseconds();}", 12, "opaque builtin methods", debug);
        Expect("const int Idle=0;const int Moving=Idle+1;int Pick(int state){switch(state){case Idle:return 1;case Moving,2:return 2;case -1:return 3;default:return 9;}}export int Probe(){return Pick(0)*1000+Pick(2)*100+Pick(-1)*10+Pick(7);}", 1239, "int switch with const labels and default", debug);
        Expect("int Pick(int kind){int result=0;switch(kind){case 1:result=10;default:;}return result;}export int Probe(){return Pick(1)*100+Pick(7);}", 1000, "empty statement as a switch default body", debug);
        Expect("int Pick(int kind){switch(kind){case 1:;case 2:return 2;default:return 0;}return 9;}export int Probe(){return Pick(1)*100+Pick(2)*10+Pick(3);}", 920, "empty statement as a case body does not fall through", debug);
        Expect("void Nothing(){;}export int Probe(){Nothing();int x=1;if(x==1);return x+1;}", 2, "empty statements in blocks and if bodies", debug);
        Expect("int Code(string command){int code=0;switch(command){case \"start\",\"resume\":code=1;break;case \"stop\":code=2;break;default:code=9;}return code;}export int Probe(){return Code(\"resume\")*100+Code(\"stop\")*10+Code(\"x\");}", 129, "string switch dispatch", debug);
        Expect("export int Probe(){int total=0;for(int i in 0,6){switch(i%3){case 0:continue;case 1:total+=10;break;default:int local=i*100;total+=local;}total+=1;}return total;}", 724, "switch break and continue inside a loop", debug);
        Expect("export int Probe(){int x=5;int seen=0;switch(x){case 1:seen=1;break;case 2:seen=2;break;}return seen;}", 0, "unmatched switch without default skips", debug);
        Expect("export int Probe(){list<int>? a=[4];int result=0;if(a!=null){switch(a[0]){case 4:a=null;break;default:result=a[0];}}return result;}", 0, "switch clause assignment invalidates nullable proof", debug);
        Expect("string? Maybe(){return \"v\";}export int Probe(){string? s=Maybe();int total=0;if(s!=null){for(int i in 0,3)total+=s.len();}return total;}", 3, "range loop keeps nullable proof", debug);
        Expect("export int Probe(){list<int>? a=[1,2];if(a==null)return -1;int total=0;int i=0;while(i<2){total+=a[i];i+=1;}foreach(int v in a)total+=v;return total;}", 6, "early return proof survives while and foreach", debug);
        Expect("export int Probe(){string? s=\"ab\";if(s==null)return 0;fn()->int f=fn()->int{return s.len();};return f();}", 2, "captured copy keeps nullable proof", debug);
        Expect("export int Probe(){string? a=\"ab\";string? b=\"c\";if(a!=null&&b!=null)return a.len()+b.len();return 0;}", 3, "conjunction refines both operands", debug);
        Expect("export int Probe(){string? a=\"ab\";string? b=\"c\";if(a==null||b==null)return 0;return a.len()*10+b.len();}", 21, "disjunction early return refines both operands", debug);
        Expect("export int Probe(){string? a=\"ab\";if(a!=null&&a.len()>1)return 1;if(a==null||a.len()>5)return 2;return 3;}", 1, "short-circuit refines the right operand", debug);
        Expect("export int Probe(){string? a=\"ab\";string? b=\"c\";if(!(a!=null&&b!=null))return 0;return a.len()+b.len();}", 3, "negated conjunction refines in the false branch", debug);
    }
    Reject("void F(){string? s=\"a\";if(s!=null){for(int i in 0,2){print(s.len());s=null;}}}", "loop body assignment drops proof for the next iteration");
    Reject("void F(){string? s=\"a\";if(s!=null){int i=0;while(i<2){print(s.len());i+=1;s=null;}}}", "while body assignment drops proof");
    Reject("void F(){string? s=\"a\";if(s!=null){fn()->int f=fn()->int{s=null;return s.len();};}}", "lambda assignment drops captured proof");
    Reject("void F(){string? a=\"a\";string? b=null;if(a!=null||b!=null)print(a.len());}", "disjunction when true proves neither operand");
    Reject("void F(){string? a=\"a\";string? b=null;if(a==null&&b==null)return;print(a.len());}", "negated conjunction proves neither operand");
    Reject("void F(){string? a=\"a\";if(a==null&&a.len()>0)return;}", "conjunction right operand under false guard is not refined");
    Reject("float f=1.0;switch(f){case 1:print(1);}", "float switch selector");
    Reject("bool b=true;switch(b){case true:print(1);}", "bool switch selector");
    Reject("string s=\"a\";switch(s){case 1:print(1);}", "int label on string selector");
    Reject("int x=1;switch(x){case 1:print(1);case 1:print(2);}", "duplicate case literal");
    Reject("const int A=2;int x=1;switch(x){case A:print(1);case 1+1:print(2);}", "duplicate case through constants");
    Reject("int x=1;switch(x){case \"a\":print(1);}", "case label type mismatch");
    Reject("int x=1;switch(x){case 1:print(1);default:print(2);default:print(3);}", "duplicate default");
    Reject("int x=1;switch(x){case 1:case 2:print(1);}", "empty switch clause");
    Reject("int x=1;int y=2;switch(x){case y:print(1);}", "non-constant case label");
    Reject("int? x=1;switch(x){case 1:print(1);}", "nullable switch selector");
    Reject("case 1:print(1);", "case outside switch");
    Reject("int F(int x){switch(x){case 1:return 1;default:break;}}", "switch break path must still return");
    Reject("int x=1;switch(x){case 1:continue;}", "continue needs a loop even inside switch");
    Reject("int x=1;switch(x){case 1:int local=1;}print(local);", "switch clause scope");
    Reject("list<int>? a=[4];if(a!=null){switch(a[0]){case 4:a=null;break;default:break;}print(a[0]);}", "nullable proof does not survive switch assignment");
    Reject("int F(int x){return x;}fn(string)->int f=F;", "incompatible function assignment");
    Reject("int F(int x){return x;}fn(int)->int f=F;f(\"wrong\");", "indirect argument type");
    Reject("fn()->int f=fn()->int{};", "missing lambda return");
    Reject("fn()->int f=fn()->int{return \"wrong\";};", "lambda return type");
    Reject("list<int> a=[];a.sort(fn(string a,string b)->bool{return a<b;});", "sort comparator type");
    Reject("import system;TimeSpan a=system.time_span_ms(1);a.add(system.stopwatch());", "opaque object mismatch");
    Reject("TimeSpan a;", "uninitialized opaque object");
    Reject("TimeSpan? a=null;print(a);", "nullable opaque values cannot be printed");
    Reject("list<int> a=[];a.append(\"wrong\");", "list append element type");
    Reject("\"abc\".regex_match(1);", "regex argument type");
    Reject("list<string>? a=\"abc\".regex_search(\"a\");print(a[0]);", "unguarded nullable collection");
    Reject("void F(){list<int>? a=[1];if(a!=null){a=null;print(a[0]);}}", "invalidated collection proof");
    Reject("int Index(list<int>? ignored){return 0;}export int Probe(){list<int>? a=[7];if(a==null)return 0;return a[Index(a=null)];}", "index arguments invalidate nullable receiver proof");
    Reject("int Value(list<int>? ignored){return 3;}void Probe(){list<int>? a=[7];if(a!=null)a.append(Value(a=null));}", "method arguments invalidate nullable receiver proof");
    Reject("import coroutine;void F(int x){}coroutine<fn(int)->void> c=coroutine.create(F);coroutine.resume(c,\"wrong\");", "coroutine argument type");
    Reject("import coroutine;int F(){return 1;}coroutine.create(F);", "coroutine return signature");
    Reject("import system;AsyncRequest a=system.async_create();a.get(1,\"url\",fn(int x,string s)->void{});", "http callback signature");
    Reject("int F(){return later;}fn()->int f=F;int later=1;", "function reference initialization order");
    std::printf("NeoCore language extensions: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
