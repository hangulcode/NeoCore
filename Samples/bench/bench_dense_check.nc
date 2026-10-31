// bench_dense.nc와 같은 함수 본문을 작은 입력으로 검증한다. 시간은 측정하지 않는다.
// 기대 체크섬은 위치를 1/512 단위 정수로 계산한 독립 모델과 정수 합으로 구했다.
// -------------------------------------------------------- 5. 배열 순차 쓰기/읽기
int ArrayRW(int size, int reps)
{
    array<int> a = array<int>(size);
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

// ------------------------------------------- 8. 파티클 시뮬 (float 배열 4개)
int Particles(int count, int steps)
{
    array<float> px = array<float>(count);
    array<float> py = array<float>(count);
    array<float> vx = array<float>(count);
    array<float> vy = array<float>(count);

    for (int i in 0, count, 1)
    {
        px[i] = (i & 63) * 1.0;
        py[i] = (i & 31) * 1.0;
        vx[i] = ((i & 7) - 4) * 0.25;
        vy[i] = ((i & 15) - 8) * 0.125;
    }

    float dt = 0.015625;   // 1/64
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

    int chk = 0;
    for (int i in 0, count, 1)
        chk += toint(px[i] + py[i] + 1000.0);
    return chk;
}

// ------------------------------------------- 8b. 파티클 시뮬 (Vector2 배열 2개)
// 성분별 연산 순서가 위와 같아서(px + vx*dt) 체크섬도 같다.
int ParticlesVec(int count, int steps)
{
    Vector2 zero = Vector2(0.0, 0.0);
    array<Vector2> pos = array<Vector2>(count, zero);
    array<Vector2> vel = array<Vector2>(count, zero);

    for (int i in 0, count, 1)
    {
        pos[i] = Vector2((i & 63) * 1.0, (i & 31) * 1.0);
        vel[i] = Vector2(((i & 7) - 4) * 0.25, ((i & 15) - 8) * 0.125);
    }

    float dt = 0.015625;   // 1/64
    for (int s in 0, steps, 1)
    {
        for (int i in 0, count, 1)
        {
            Vector2 p = pos[i] + vel[i] * dt;
            float x = p.x;                  // 성분을 지역에 꺼내야 두 비교가 범위 점프 하나로 합쳐진다
            float y = p.y;
            if (x < 0.0 || x > 64.0)
                vel[i].x = -vel[i].x;
            if (y < 0.0 || y > 32.0)
                vel[i].y = -vel[i].y;
            pos[i] = p;
        }
    }

    int chk = 0;
    for (int i in 0, count, 1)
    {
        Vector2 p = pos[i];
        chk += toint(p.x + p.y + 1000.0);
    }
    return chk;
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

Check("array_empty", ArrayRW(0, 3), 0);
Check("array_rw", ArrayRW(257, 3), 32640);
Check("array_bias", ArrayRW(258, 3), 32643);
Check("particles_empty", Particles(0, 16), 0);
Check("particles_vec_empty", ParticlesVec(0, 16), 0);
Check("particles_16", Particles(80, 16), 83205);
Check("particles_vec_16", ParticlesVec(80, 16), 83205);
Check("particles_200", Particles(80, 200), 83197);
Check("particles_vec_200", ParticlesVec(80, 200), 83197);

if (allPassed) print("dense checks ok");
