import math;

float clamped = math.Clamp(1.5, 0, 1);
float blended = math.Lerp(10, 20, 0.25);
float angle = math.atan2(1, 0);
int tint = math.ColorARGB(0.5, 1, 0, 0);
Vector3 direction = math.Normalize3(math.Vector3(0, 3, 4), Vector3(1, 0, 0));
Vector3 crossed = math.Cross3(Vector3(1, 0, 0), Vector3(0, 1, 0));
Quaternion rotation = math.quat_from_basis(Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
Vector3 rotated = math.RotateVectorByQuat(rotation, direction);

math.srand(123);
float randomValue = math.RandRange(-2, 2);
math.srand(123);
float repeatedValue = math.RandRange(-2, 2);
float preciseValue = 0.1 + 0.2;
string preciseText = math.tostr(preciseValue);

print("clamp: " .. tostring(clamped));
print("lerp: " .. tostring(blended));
print("angle: " .. math.tostr(angle));
print("color: " .. tostring(tint));
print("random: " .. math.tostr(randomValue));
print("round trip: " .. preciseText);

export bool CheckPassed()
{
    return clamped == 1 && blended == 12.5
        && math.abs(angle - 1.5707963267948966) < 0.000000000001
        && tint == 2130706687 && crossed.z == 1
        && math.abs(rotated.y - 0.6) < 0.000001
        && math.abs(rotated.z - 0.8) < 0.000001
        && randomValue >= -2 && randomValue <= 2 && randomValue == repeatedValue
        && tofloat(preciseText) == preciseValue;
}
