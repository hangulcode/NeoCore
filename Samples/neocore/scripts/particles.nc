bool CheckParticles()
{
    int count = 64;
    int steps = 128;
    float dt = 0.25;
    array<Vector2> positions = array<Vector2>(count);
    array<Vector2> velocities = array<Vector2>(count);
    array<float> px = array<float>(count);
    array<float> py = array<float>(count);
    array<float> vx = array<float>(count);
    array<float> vy = array<float>(count);

    // 위치와 속도는 작은 이진 유리수다. 모든 중간값이 float32에도 정확히 들어간다.
    for (int i in 0, count)
    {
        px[i] = (i & 7) * 8.0;
        py[i] = (i & 3) * 8.0;
        vx[i] = ((i & 3) - 1) * 0.5;
        vy[i] = ((i & 7) - 3) * 0.25;
        positions[i] = Vector2(px[i], py[i]);
        velocities[i] = Vector2(vx[i], vy[i]);
    }

    // 벡터 배열 경로를 스칼라 배열 경로와 매 단계 비교한다. 시간은 측정하지 않는다.
    for (int step in 0, steps)
    {
        for (int i in 0, count)
        {
            positions[i] += velocities[i] * dt;
            float x = positions[i].x;
            float y = positions[i].y;
            if (x < 0.0 || x > 64.0)
                velocities[i].x = -velocities[i].x;
            if (y < 0.0 || y > 32.0)
                velocities[i].y = -velocities[i].y;

            px[i] += vx[i] * dt;
            py[i] += vy[i] * dt;
            if (px[i] < 0.0 || px[i] > 64.0)
                vx[i] = -vx[i];
            if (py[i] < 0.0 || py[i] > 32.0)
                vy[i] = -vy[i];

            if (x != px[i] || y != py[i] ||
                velocities[i].x != vx[i] || velocities[i].y != vy[i])
            {
                print("FAIL particles vector state");
                return false;
            }
        }
    }

    int checksum = 0;
    for (int i in 0, count)
    {
        checksum += toint(positions[i].x * 16.0);
        checksum += toint(positions[i].y * 16.0) * 3;
        checksum += toint((velocities[i].x + 4.0) * 16.0) * 5;
        checksum += toint((velocities[i].y + 4.0) * 16.0) * 7;
    }
    if (checksum != 133840)
    {
        print("FAIL particles checksum: " .. tostring(checksum));
        return false;
    }

    // 배열 별칭은 저장소를 공유하지만 꺼낸 벡터는 독립적인 값이다.
    array<Vector2> alias = positions;
    Vector2 original = positions[0];
    Vector2 detached = positions[0];
    detached.x += 2.0;
    if (positions[0].x != original.x || detached.x != original.x + 2.0)
    {
        print("FAIL detached vector copy");
        return false;
    }
    alias[0].x += 1.0;
    if (positions[0].x != original.x + 1.0 || alias[0].x != positions[0].x ||
        positions[0].y != original.y || original.x != px[0])
    {
        print("FAIL shared array component write");
        return false;
    }

    print("particles checksum|" .. tostring(checksum));
    return true;
}

if (CheckParticles())
    print("particles vectors ok");
