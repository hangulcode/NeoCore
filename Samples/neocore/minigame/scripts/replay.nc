import math;

// 하나의 컴파일 결과를 공유해도 원점·시간·속도·콜백은 인스턴스마다 따로 보관한다.
Vector3 origin = this.Position;
float elapsed = 0.0;
float speed = 1.0;
int updates = 0;

if (this.Name == "Blue") speed = -0.5;

void OnUpdate(float dt)
{
    elapsed += dt;
    updates += 1;
    Vector3 position = this.Position;
    position.x += speed * dt;
    position.y = origin.y + math.sin(elapsed * 3.0) * 0.5;
    this.Position = position;
    if (updates == 60)
        Services.Log(this.Name .. ": 60 updates in my own instance");
}

export int UpdateCount()
{
    return updates;
}

this.AddEvent("OnUpdate", OnUpdate);
Services.Log(this.Name .. ": script attached");
