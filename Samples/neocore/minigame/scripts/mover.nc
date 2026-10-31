float speed = 1.0;
int updates = 0;
int events = 0;
int wakeups = 0;
Vector3 origin = this.Position;

void OnUpdate(float dt)
{
    updates += 1;
    this.Position.x += speed * dt;
}

void OnEvent(int strength)
{
    events += 1;
    speed = float(strength);
    Services.Record(this.Id, "event");
}

void DelayedEvent()
{
    GameObject target = this;
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

export void SubscribeTo(GameObject target) { target.AddEvent("OnEvent", OnEvent); }

this.AddEvent("OnUpdate", OnUpdate);
this.AddEvent("OnEvent", OnEvent);
this.AddEvent("DelayedEvent", DelayedEvent);
