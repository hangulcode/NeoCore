// mapbench.nc와 같은 함수 본문을 작은 입력으로 검증한다. 시간은 측정하지 않는다.
// 기대 합은 n*(n-1)/2*reps에 같은 마스크를 적용하고, 삽입·삭제는 길이로 확인한다.
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

// 작은 입력으로 누산 마스크, 비어 있는 맵, 순회 키·값과 실제 삭제를 확인한다.
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

int SparseIteration()
{
    map<int, int> m = {};
    m[2] = 10;
    m[7] = 20;
    m[13] = 30;
    int weighted = 0;
    int count = 0;
    foreach (int key, int value in m)
    {
        weighted += key * value;
        count += 1;
        if (m.get(key, -1) != value) return -1;
    }
    if (count != 3) return -2;
    return weighted;
}

int Removal()
{
    map<int, int> m = {};
    m[2] = 10;
    m[7] = 20;
    if (!m.remove(2)) return -1;
    if (m.len() != 1 || m.contains(2)) return -2;
    if (m.remove(2)) return -3;
    if (!m.remove(7)) return -4;
    if (m.len() != 0) return -5;
    m[2] = 30;
    if (m.len() != 1 || m.get(2, -1) != 30) return -6;
    return 1;
}

export bool CheckPassed()
{
    return allPassed;
}

Check("map_ins_int", InsertInt(257), 257);
Check("map_ins_str", InsertStr(257), 257);
Check("map_lookup", LookupInt(1025, 3), 525824);
Check("map_iterate", IterBig(1025, 3), 525824);
Check("map_churn", Churn(257, 4), 1028);
Check("map_empty_lookup", LookupInt(0, 3), 0);
Check("map_empty_iterate", IterBig(0, 3), 0);
Check("map_empty_churn", Churn(0, 3), 0);
Check("map_sparse_iteration", SparseIteration(), 550);
Check("map_removal", Removal(), 1);

if (allPassed) print("map checks ok");
