// bench_call.nc와 같은 함수 본문을 작은 입력으로 검증한다. 시간은 측정하지 않는다.
// 각 루프의 마지막 합은 안쪽 반복 횟수이며, 반복이 없으면 0이다.
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

// 작은 입력으로만 실행하며 시계를 호출하지 않는다.
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

Check("call_floor", LoopFloor(3, 37), 37);
Check("call_floor_empty_outer", LoopFloor(0, 37), 0);
Check("call_floor_empty_inner", LoopFloor(3, 0), 0);
Check("call_void", LoopVoid(3, 37), 37);
Check("call_void_empty_outer", LoopVoid(0, 37), 0);
Check("call_void_empty_inner", LoopVoid(3, 0), 0);
Check("call_ret0", LoopRet0(3, 37), 37);
Check("call_ret0_empty_outer", LoopRet0(0, 37), 0);
Check("call_ret0_empty_inner", LoopRet0(3, 0), 0);
Check("call_ret1", LoopRet1(3, 37), 37);
Check("call_ret1_empty_outer", LoopRet1(0, 37), 0);
Check("call_ret1_empty_inner", LoopRet1(3, 0), 0);
Check("call_ret3", LoopRet3(3, 37), 37);
Check("call_ret3_empty_outer", LoopRet3(0, 37), 0);
Check("call_ret3_empty_inner", LoopRet3(3, 0), 0);

if (allPassed) print("call checks ok");
