export void Update()
{
    Vector3 position = g_sData.Transform;
    position.x += 2.0;
    g_sData.Transform = position;
    print(tostring(position.x) .. "," .. tostring(position.y) .. "," .. tostring(position.z));
}
