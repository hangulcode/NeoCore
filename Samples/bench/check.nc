// ============================================================================
// NeoCore의 기존 8개 벤치 알고리즘을 작은 입력으로 검증한다. 시간은 측정하지 않는다.
// 아래 함수 본문은 bench.nc와 같고, 입력 크기와 출력만 검증용으로 줄였다.
// 기대값은 별도의 Python 정수 계산·Fraction 유리수 모델로 계산했다.
// 정상 출력은 이름|체크섬이다. 불일치하면 FAIL 행을 추가하고 CheckPassed가 false를 반환한다.
// ============================================================================
import math;
import system;

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
// 정적 맵의 직접 읽기는 nullable이므로 get(key, 0)으로 누락 시 값을 명시한다.
// 모든 조회 키를 위에서 채우므로 이 기본값은 체크섬에 영향을 주지 않는다.
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
            sum += m.get("alpha", 0);
            sum += m.get("hotel", 0);
            sum += m.get("charlie", 0);
            sum += m.get("foxtrot", 0);
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

// 작은 입력으로만 실행하며 예열·반복 측정·시계 호출은 하지 않는다.
bool allPassed = true;
void Check(string name, int actual, int expected)
{
    print(name .. "|" .. tostring(actual));
    if (actual != expected)
    {
        allPassed = false;
        print("FAIL|" .. name .. "|expected=" .. tostring(expected));
    }
}

export bool CheckPassed()
{
    return allPassed;
}

Check("loop_sum", LoopSum(3, 32), 496);
Check("float_math", FloatMath(3, 32), 133);
Check("func_call", FuncCall(3, 32), 528);
Check("fib_recursive", Fib(12), 144);
Check("array_rw", ArrayRW(257, 3), 32640);
Check("map_str", MapStr(3, 32), 576);
Check("string_ops", StringOps(2, 40), 230);
Check("particles", Particles(80, 16), 83205);
