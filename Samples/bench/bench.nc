// ============================================================================
// NeoCore 성능 벤치마크 — C++/Lua 비교판과 같은 알고리즘과 반복량을 사용한다.
//
// 기본 int는 int64, float는 float64다. 자료구조는 정수·실수 목록과 문자열 키 맵이다.
// 각 언어의 시계로 실행 구간만 측정하므로 프로세스 기동·컴파일 시간은 제외한다.
// 기존과 같은 작은 예열 후 REPS=5 중 최소값을 보고한다.
// 체크섬은 언어 사이 결과 대조용이며, 안쪽 누산을 반복마다 초기화하는 원래 순서를 유지한다.
// 맵 읽기는 정적 타입의 누락 가능성을 처리하려고 get(key, 0)을 쓴다.
// 문자열 연결의 숫자 변환은 tostring으로 명시한다. 출력은 이름|마이크로초|체크섬이다.
// ============================================================================
import math;
import system;

int REPS = 5;

// ---------------------------------------------------------------- 1. 정수 루프
// VM 디스패치의 순수 비용. inner 합 = 12,497,500.
int LoopSum(int outer, int inner)
{
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int bias = last & 1;           // 직전 결과에 의존(진짜 데이터 의존성) → 축약 불가
        int sum = 0;
        for (int i in 0, inner, 1)
            sum += i + bias;
        last = sum;
    }
    return last;
}

// ------------------------------------------------------------- 2. 부동소수 산술
// mul/add/sub 혼합. 상수·피연산자를 모두 **이진 정확값**(1/2, 1/4 배수)으로 잡아
// float64(NeoCore)와 double(Lua/C++)이 같은 체크섬을 내도록 한다.
int FloatMath(int outer, int inner)
{
    float last = 0.0;
    for (int o in 0, outer, 1)
    {
        int bias = toint(last) & 1;    // 직전 결과 의존
        float acc = 0.0;
        for (int i in 0, inner, 1)
        {
            float x = ((i + bias + toint(acc)) & 15) * 0.5;   // acc 의존 → 예측 불가
            acc += x * 1.5 - x * 0.25;
        }
        last = acc;
    }
    return toint(last);
}

// ---------------------------------------------------------------- 3. 함수 호출
int Inc(int x)
{
    return x + 1;
}
int FuncCall(int outer, int inner)
{
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int bias = last & 1;           // 직전 결과 의존
        int sum = 0;
        for (int i in 0, inner, 1)
            sum += Inc(i + bias);
        last = sum;
    }
    return last;
}

// ------------------------------------------------------------------- 4. 재귀
int Fib(int n)
{
    if (n < 2)
        return n;
    return Fib(n - 1) + Fib(n - 2);
}

// -------------------------------------------------------- 5. 배열 순차 쓰기/읽기
int ArrayRW(int size, int reps)
{
    list<int> a = [];
    a.resize(size);
    int last = 0;
    for (int r in 0, reps, 1)
    {
        int bias = last & 1;           // 직전 결과 의존
        for (int i in 0, size, 1)
            a[i] = (i + bias) & 255;
        int sum = 0;
        for (int i in 0, size, 1)
            sum += a[i];
        last = sum;
    }
    return last;
}

// ------------------------------------------------------- 6. 해시맵(문자열 키) 조회
// 모든 조회 키를 위에서 채운다. 없는 키를 읽으면 실행 오류가 난다.
int MapStr(int outer, int inner)
{
    map<string,int> m = {};
    m["alpha"] = 1;
    m["bravo"] = 2;
    m["charlie"] = 3;
    m["delta"] = 4;
    m["echo"] = 5;
    m["foxtrot"] = 6;
    m["golf"] = 7;
    m["hotel"] = 8;
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int sum = 0;
        for (int i in 0, inner, 1)
        {
            sum += m["alpha"];
            sum += m["hotel"];
            sum += m["charlie"];
            sum += m["foxtrot"];
        }
        last = sum;
    }
    return last;
}

// ----------------------------------------------------------- 7. 문자열 생성/길이
int StringOps(int outer, int inner)
{
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int total = 0;
        for (int i in 0, inner, 1)
        {
            string s = "item" .. tostring(i);
            total += s.len();
        }
        last = total;
    }
    return last;
}

// ------------------------------------------- 8. 파티클 시뮬(게임형 부동소수+배열)
// pos += vel*dt, 경계 반사. 게임 스크립트에서 가장 흔한 형태의 워크로드.
int Particles(int count, int steps)
{
    list<float> px = []; px.resize(count);
    list<float> py = []; py.resize(count);
    list<float> vx = []; vx.resize(count);
    list<float> vy = []; vy.resize(count);

    for (int i in 0, count, 1)
    {
        px[i] = (i & 63) * 1.0;
        py[i] = (i & 31) * 1.0;
        vx[i] = ((i & 7) - 4) * 0.25;
        vy[i] = ((i & 15) - 8) * 0.125;
    }

    float dt = 0.015625;   // 1/64 — 기존 체크섬 비교용 이진 정확값을 유지한다.
    for (int s in 0, steps, 1)
    {
        for (int i in 0, count, 1)
        {
            float nx = px[i] + vx[i] * dt;
            float ny = py[i] + vy[i] * dt;
            if (nx < 0.0 || nx > 64.0)
                vx[i] = -vx[i];
            if (ny < 0.0 || ny > 32.0)
                vy[i] = -vy[i];
            px[i] = nx;
            py[i] = ny;
        }
    }

    // +1000 오프셋: 위치가 음수가 될 수 있어(반사 직전) truncate/floor 차이가 생기는 것을 막는다.
    int chk = 0;
    for (int i in 0, count, 1)
        chk += toint(px[i] + py[i] + 1000.0);
    return chk;
}

// ============================================================================
// 측정 하네스 — 예열 1회 + REPS 회 측정 후 최소값(마이크로초)
// ============================================================================
void Report(string name, float best, int chk)
{
    print(name .. "|" .. tostring(toint(best * 1000000.0)) .. "|" .. tostring(chk));
}

print("# neocore bench begin");

// 1. loop_sum : 10,000 x 5,000 = 5천만 회
LoopSum(2, 5000);
float best = 1000000.0;
int chk = 0;
for (int r in 0, REPS, 1)
{
    float t0 = system.clock();
    chk = LoopSum(10000, 5000);
    float dt = system.clock() - t0;
    if (dt < best) best = dt;
}
Report("loop_sum", best, chk);

// 2. float_math : 2,000 x 5,000 = 1천만 회
FloatMath(2, 5000);
best = 1000000.0;
for (int r in 0, REPS, 1)
{
    float t0 = system.clock();
    chk = FloatMath(2000, 5000);
    float dt = system.clock() - t0;
    if (dt < best) best = dt;
}
Report("float_math", best, chk);

// 3. func_call : 2,000 x 5,000 = 1천만 회
FuncCall(2, 5000);
best = 1000000.0;
for (int r in 0, REPS, 1)
{
    float t0 = system.clock();
    chk = FuncCall(2000, 5000);
    float dt = system.clock() - t0;
    if (dt < best) best = dt;
}
Report("func_call", best, chk);

// 4. fib(32) = 2178309
Fib(10);
best = 1000000.0;
for (int r in 0, REPS, 1)
{
    float t0 = system.clock();
    chk = Fib(32);
    float dt = system.clock() - t0;
    if (dt < best) best = dt;
}
Report("fib_recursive", best, chk);

// 5. array_rw : 200,000 x 20 = 400만 쓰기 + 400만 읽기
ArrayRW(1000, 1);
best = 1000000.0;
for (int r in 0, REPS, 1)
{
    float t0 = system.clock();
    chk = ArrayRW(200000, 20);
    float dt = system.clock() - t0;
    if (dt < best) best = dt;
}
Report("array_rw", best, chk);

// 6. map_str : 500 x 5,000 x 4 = 1천만 조회
MapStr(2, 1000);
best = 1000000.0;
for (int r in 0, REPS, 1)
{
    float t0 = system.clock();
    chk = MapStr(500, 5000);
    float dt = system.clock() - t0;
    if (dt < best) best = dt;
}
Report("map_str", best, chk);

// 7. string_ops : 20 x 50,000 = 100만 문자열 생성
StringOps(1, 1000);
best = 1000000.0;
for (int r in 0, REPS, 1)
{
    float t0 = system.clock();
    chk = StringOps(20, 50000);
    float dt = system.clock() - t0;
    if (dt < best) best = dt;
}
Report("string_ops", best, chk);

// 8. particles : 10,000 파티클 x 200 스텝 = 200만 업데이트
Particles(100, 2);
best = 1000000.0;
for (int r in 0, REPS, 1)
{
    float t0 = system.clock();
    chk = Particles(10000, 200);
    float dt = system.clock() - t0;
    if (dt < best) best = dt;
}
Report("particles", best, chk);

print("# neocore bench end");
