import system;
import math;

void Check(bool valid)
{
    if (!valid) { int zero = 0; print(1 / zero); }
}
int Fibonacci(int n)
{
    if (n < 2) return n;
    return Fibonacci(n - 1) + Fibonacci(n - 2);
}
struct Position { float x; float y; };
Position Shift(Position value) { value.x += 3.0; return value; }

Check(Fibonacci(12) == 144);
Check((17 % 5) == 2 && ((7 << 2) | 1) == 29);
Check(math.sqrt(81) == 9.0);
Check(math.fixed(12.75, 1) == "12.8" && math.fixed(2.0, 1) == "2.0");
Check(math.fixed(0.07, 1) == "0.1" && math.fixed(-0.05, 1) == "-0.1");
Check(math.fixed(-0.04, 1) == "0.0" && math.fixed(-0.0, 2) == "0.00");
Check(math.fixed(0.25, 1) == "0.2" && math.fixed(0.15, 1) == "0.1");
Check(math.fixed(2.5, 0) == "2" && math.fixed(-2.5, 0) == "-2");
Check(math.fixed(3.5, 0) == "4" && math.fixed(0.125, 2) == "0.12");
Check(math.fixed(123456.789, 1) == "123456.8" && math.fixed(1.0 / 3.0, 3) == "0.333");
Check(math.fixed(0.0000001, 2) == "0.00" && math.fixed(1.0e20, 1) == "100000000000000000000.0");
Check(math.fixed(42, 2) == "42.00" && math.fixed(math.Vector2(0.1, 0.0).x, 3) == "0.100");
int sum = 0;
for (int i in 10, 0, -1)
{
    if (i == 7) continue;
    if (i == 2) break;
    sum += i;
}
Check(sum == 45);
Position source = Position(1, 2);
Position moved = Shift(source);
Check(source.x == 1.0 && moved.x == 4.0);
array<Vector3> vectors = array<Vector3>(2, Vector3(1, 2, 3));
vectors[0].x += 4.0;
Check(vectors[0].x == 5.0 && vectors[1].x == 1.0);
list<int> values = [3, 1, 2];
values.sort(fn(int a, int b)->bool { return a < b; });
Check(values[0] == 1 && values[2] == 3);
map<string, int> counts = {"one": 1, "two": 2};
Check(counts.contains("one") && counts.remove("one") && counts.len() == 1);
int optionalCount = 99;
Check(counts.try_get("two", out optionalCount) && optionalCount == 2);
Check(!counts.try_get("missing", out optionalCount) && optionalCount == 2);
set<int> unique = system.set(values);
Check(unique.len() == 3 && unique.contains(2));
Check("AbC".lower() == "abc" && "item42".regex_match("[a-z]+[0-9]+"));
fn(int)->int twice = fn(int x)->int { return x * 2; };
Check(twice(21) == 42);
print("regression: typed arithmetic, control flow, values, collections and callback checks passed");
