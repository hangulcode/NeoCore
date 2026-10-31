float speed = 1.0;
int updates = 0;
int events = 0;
int wakeups = 0;
Vector3 origin = GameObject.Position;

export void OnUpdate(float dt)
{
    updates += 1;
    GameObject.Position.x += speed * dt;
}

export void OnEvent(int strength)
{
    events += 1;
    speed = float(strength);
    Services.Record(GameObject.Id, "event");
}

export void DelayedEvent()
{
    GameObject target = GameObject;
    // 0도 실행을 양보한다. 저장한 ID는 재개 후 멤버에 접근할 때 다시 검증한다.
    sleep(0);
    target.Position.y += 1.0;
    wakeups += 1;
    Services.Record(target.Id, "awake");
}

export int UpdateCount() { return updates; }
export int EventCount() { return events; }
export int WakeCount() { return wakeups; }
export float OriginX() { return origin.x; }
