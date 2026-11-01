// 서로 다른 값 영역은 각각의 정적 맵으로 표현한다.
map<int, int> numbers = {};
map<int, map<int, int>> children = {};
map<string, string> labels = {};
map<int, int> child = {};
child[2] = 999;
child[3] = 888;
children[6] = child;
numbers[5] = 3;
labels["c3"] = "Hello NeoCore...";
labels["c4"] = "system";
labels.remove("c4");
for (int i in 0, 10)
{
    if (i % 2 == 0)
    {
        map<int, int> empty = {};
        children[i] = empty;
    }
    else numbers[i] = i;
}
children.remove(0);
foreach (int key, int value in numbers) print(tostring(key) .. ":" .. tostring(value));
print("Map Size : " .. tostring(numbers.len() + children.len() + labels.len()));

map<int, int> sortable = {};
sortable[0] = 5;
sortable[1] = 3;
sortable[2] = 1;
sortable[3] = 4;
sortable[4] = 7;
sortable[5] = 2;
sortable.sort(fn(int a, int b)->bool { return a < b; });
print("Ascending:");
foreach (int value in sortable.values()) print(value);

map<string, string> james = {};
james["nationality"] = "America";
james["name"] = "James";
map<string, string> alice = {};
alice["nationality"] = "Korea";
alice["name"] = "Alice";
map<string, string> donald = {};
donald["nationality"] = "Japan";
donald["name"] = "Donald";
map<int, map<string, string>> people = {};
people[0] = james;
people[1] = alice;
people[2] = donald;
people.sort(fn(map<string, string> a, map<string, string> b)->bool { return a["name"] < b["name"]; });
print("By name:");
foreach (map<string, string> person in people.values()) print(person["name"]);
people.sort(fn(map<string, string> a, map<string, string> b)->bool { return a["nationality"] < b["nationality"]; });
print("By nationality:");
foreach (map<string, string> person in people.values()) print(person["nationality"]);

map<int, int> many = {};
for (int i in 1, 10001) many[i] = i;
many.sort(fn(int a, int b)->bool { return a > b; });
int total = 0;
foreach (int key, int value in many) total += value;
if (many.len() != 10000 || total != 50005000) { int zero = 0; print(1 / zero); }
print("Sum " .. tostring(total));
