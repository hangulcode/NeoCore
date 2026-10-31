import math;

int ticks = 0;
Vector3 position;

Vector3 Wobble(float time)
{
    return Vector3(math.sin(time) * 2.0, 1.0, math.cos(time) * 2.0);
}

export bool Update(float dt, float time)
{
    ticks += 1;
    position = Wobble(time);
    position.y += dt;
    return ticks == 1;
}

export float PositionX() { return position.x; }
export float PositionY() { return position.y; }
export float PositionZ() { return position.z; }
export int Ticks() { return ticks; }
