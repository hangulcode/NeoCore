// NeoCore 함수 호출 분해 벤치. 다섯 호출 경로를 같은 반복량으로 비교한다.
// 호출 함수는 반환 없음 또는 상수 1 반환으로 고정하고 인자 수만 바꾼다.
// 정적 호출·반환 경로를 측정하므로 기존 NeoScript 측정값과 동일한 비용을 뜻하지 않는다.
// 실행 전에 bench_call_check.nc로 체크섬을 확인하고 측정 실행은 사용자 동의를 받는다.
import system;

int REPS = 5;

// ------------------------------------------------------------- 피호출 함수들
void CallV()                                { }            // 반환값 없음
int CallR0()                               { return 1; }
int CallR1(int a)                          { return 1; }
int CallR3(int a, int b, int c)            { return 1; }

// ------------------------------------------------------------------ 측정 루프
int LoopFloor(int outer, int inner)
{
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int sum = 0;
        for (int i in 0, inner, 1)
            sum += 1;
        last = sum;
    }
    return last;
}

int LoopVoid(int outer, int inner)
{
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int sum = 0;
        for (int i in 0, inner, 1)
        {
            CallV();
            sum += 1;
        }
        last = sum;
    }
    return last;
}

int LoopRet0(int outer, int inner)
{
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int sum = 0;
        for (int i in 0, inner, 1)
            sum += CallR0();
        last = sum;
    }
    return last;
}

int LoopRet1(int outer, int inner)
{
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int sum = 0;
        for (int i in 0, inner, 1)
            sum += CallR1(i);
        last = sum;
    }
    return last;
}

int LoopRet3(int outer, int inner)
{
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int sum = 0;
        for (int i in 0, inner, 1)
            sum += CallR3(i, i, i);
        last = sum;
    }
    return last;
}

void Report(string name, float best, int chk)
{
    print(name .. "|" .. tostring(toint(best * 1000000.0)) .. "|" .. tostring(chk));
}

print("# neocore call-decomp begin");

int OUTER = 2000;
int INNER = 5000;
float best = 0.0;
int chk = 0;

LoopFloor(2, INNER);                                  // 예열
best = 1000000.0;
for (int r1 in 0, REPS, 1)
{
    float t1 = system.clock();
    chk = LoopFloor(OUTER, INNER);
    float d1 = system.clock() - t1;
    if (d1 < best) best = d1;
}
Report("call_floor", best, chk);

LoopVoid(2, INNER);
best = 1000000.0;
for (int r2 in 0, REPS, 1)
{
    float t2 = system.clock();
    chk = LoopVoid(OUTER, INNER);
    float d2 = system.clock() - t2;
    if (d2 < best) best = d2;
}
Report("call_void", best, chk);

LoopRet0(2, INNER);
best = 1000000.0;
for (int r3 in 0, REPS, 1)
{
    float t3 = system.clock();
    chk = LoopRet0(OUTER, INNER);
    float d3 = system.clock() - t3;
    if (d3 < best) best = d3;
}
Report("call_ret0", best, chk);

LoopRet1(2, INNER);
best = 1000000.0;
for (int r4 in 0, REPS, 1)
{
    float t4 = system.clock();
    chk = LoopRet1(OUTER, INNER);
    float d4 = system.clock() - t4;
    if (d4 < best) best = d4;
}
Report("call_ret1", best, chk);

LoopRet3(2, INNER);
best = 1000000.0;
for (int r5 in 0, REPS, 1)
{
    float t5 = system.clock();
    chk = LoopRet3(OUTER, INNER);
    float d5 = system.clock() - t5;
    if (d5 < best) best = d5;
}
Report("call_ret3", best, chk);

print("# neocore call-decomp end");
