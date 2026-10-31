// NeoCore 내장 배열 벤치마크. 배열 저장 형태별로 같은 알고리즘과 반복량을 사용한다.
// 정수·실수 배열과 Vector2 배열은 명시적인 원소 타입을 사용한다.
// 스칼라 실수는 float64, Vector2 성분은 float32이며 이 입력은 이진수로 정확하다.
// 실행 전에 bench_dense_check.nc로 체크섬을 확인하고 측정 실행은 사용자 동의를 받는다.
import system;

int REPS = 5;

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

// ============================================================================
void Report(string name, float best, int chk)
{
    print(name .. "|" .. tostring(toint(best * 1000000.0)) .. "|" .. tostring(chk));
}

print("# neocore dense bench begin");
float best;
int chk;

ArrayRW(1000, 1);
best = 1000000.0; chk = 0;
for (int r in 0, REPS, 1)
{
    float t = system.clock();
    chk = ArrayRW(200000, 20);
    float e = system.clock() - t;
    if (e < best) best = e;
}
Report("array_rw", best, chk);

Particles(100, 2);
best = 1000000.0;
for (int r in 0, REPS, 1)
{
    float t = system.clock();
    chk = Particles(10000, 200);
    float e = system.clock() - t;
    if (e < best) best = e;
}
Report("particles", best, chk);

ParticlesVec(100, 2);
best = 1000000.0;
for (int r in 0, REPS, 1)
{
    float t = system.clock();
    chk = ParticlesVec(10000, 200);
    float e = system.clock() - t;
    if (e < best) best = e;
}
Report("particles_vec", best, chk);
print("# neocore dense bench end");
