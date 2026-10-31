// NeoCore 대량 맵 벤치. mapbench.ns의 다섯 알고리즘과 반복량을 유지한다.
// 맵은 키·값 타입을 명시하고 nullable 조회 대신 get(key, 0)을 사용한다.
// 순회는 foreach, 삭제는 remove로 표현하며 원래 삽입·삭제 순서를 유지한다.
// 실행 전에 mapbench_check.nc로 체크섬을 확인하고 측정 실행은 사용자 동의를 받는다.
import system;

int REPS = 5;
int N = 200000;

void Report(string name, float sec, int chk) { print(name .. "|" .. tostring(sec * 1000000) .. "|" .. tostring(chk)); }

// 1) 정수 키 대량 삽입. 맵 크기를 늘리며 재해시를 포함한 삽입을 수행한다.
int InsertInt(int n)
{
    map<int, int> m = {};
    for (int i in 0, n, 1) m[i] = i;
    return m.len();
}

// 2) 문자열 키 대량 삽입. 위에 더해 키 인터닝과 해시 계산이 붙는다.
int InsertStr(int n)
{
    map<string, int> m = {};
    for (int i in 0, n, 1) m["k" .. tostring(i)] = i;
    return m.len();
}

// 3) 대량 조회. 모든 키를 채운 뒤 명시적 기본값을 둔 get으로 읽는다.
int LookupInt(int n, int reps)
{
    map<int, int> m = {};
    for (int i in 0, n, 1) m[i] = i;
    int s = 0;
    for (int r in 0, reps, 1)
    {
        for (int i in 0, n, 1) s = (s + m.get(i, 0)) & 1048575;
    }
    return s;
}

// 4) 대량 순회. 실제 맵 반복자를 사용해 저장된 값을 모두 누산한다.
int IterBig(int n, int reps)
{
    map<int, int> m = {};
    for (int i in 0, n, 1) m[i] = i;
    int s = 0;
    for (int r in 0, reps, 1)
    {
        foreach (int k, int v in m) s = (s + v) & 1048575;
    }
    return s;
}

// 5) 삽입/삭제 반복. 삭제는 용량을 줄이지 않으므로 빈 슬롯이 쌓인 상태의 재삽입을 잰다.
int Churn(int n, int reps)
{
    map<int, int> m = {};
    int t = 0;
    for (int r in 0, reps, 1)
    {
        for (int i in 0, n, 1) m[i] = i;
        t = (t + m.len()) & 1048575;
        for (int i in 0, n, 1) m.remove(i);
    }
    return t;
}

print("# neocore mapbench begin");
float best; int chk;

InsertInt(100);
best = 1000000.0;
for (int r in 0, REPS, 1) { float t0 = system.clock(); chk = InsertInt(N); float dt = system.clock() - t0; if (dt < best) best = dt; }
Report("map_ins_int", best, chk);

InsertStr(100);
best = 1000000.0;
for (int r in 0, REPS, 1) { float t0 = system.clock(); chk = InsertStr(N); float dt = system.clock() - t0; if (dt < best) best = dt; }
Report("map_ins_str", best, chk);

LookupInt(100, 1);
best = 1000000.0;
for (int r in 0, REPS, 1) { float t0 = system.clock(); chk = LookupInt(N, 5); float dt = system.clock() - t0; if (dt < best) best = dt; }
Report("map_lookup", best, chk);

IterBig(100, 1);
best = 1000000.0;
for (int r in 0, REPS, 1) { float t0 = system.clock(); chk = IterBig(N, 5); float dt = system.clock() - t0; if (dt < best) best = dt; }
Report("map_iterate", best, chk);

Churn(100, 1);
best = 1000000.0;
for (int r in 0, REPS, 1) { float t0 = system.clock(); chk = Churn(50000, 10); float dt = system.clock() - t0; if (dt < best) best = dt; }
Report("map_churn", best, chk);

print("# neocore mapbench end");
