struct Transform
{
    Vector3 position;
    Quaternion rotation;
};

Vector3 Move(Vector3 position, float amount)
{
    position.x += amount;
    return position;
}

Vector3 position = Vector3(1, 2, 3);
Vector3 snapshot = position;
position = Move(position, 2.0);

Transform transform;
transform.position = position;
transform.position.z = 5.0;

print(tostring(position.x) .. " " .. tostring(snapshot.x) .. " " ..
    tostring(transform.position.z) .. " " .. tostring(transform.rotation.w));
