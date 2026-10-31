import system;

list<int> first = [2, 4, 5, 8];
list<int> second = [1, 4, 5, 7];
first.append(99);
first.append(-99, 2);
list<int> combined = first + second;
print("List append and concatenation:");
foreach (int value in combined) print(value, " ");
print("");

list<int> leftValues = [2, 4, 5, 8];
list<int> rightValues = [1, 4, 5, 7];
set<int> left = system.set(leftValues);
set<int> right = system.set(rightValues);
set<int> united = left | right;
set<int> intersection = left & right;
set<int> difference = left - right;
print("Set union:");
foreach (int value in united) print(value, " ");
print("\nSet intersection:");
foreach (int value in intersection) print(value, " ");
print("\nSet difference:");
foreach (int value in difference) print(value, " ");
print("");
if (united.len() != 6 || intersection.len() != 2 || difference.len() != 2) { int zero = 0; print(1 / zero); }

map<string, int> map1 = {};
map1["a"] = 1;
map1["b"] = 4;
map1["c"] = 5;
map1["d"] = 8;
map<string, int> map2 = {};
map2["a"] = 3;
map2["X"] = 4;
map2["Y"] = 5;
print("Map:");
foreach (string key, int value in map1) print(key .. "=" .. tostring(value), " ");
print("\nMap keys:");
foreach (string key in map1.keys()) print(key, " ");
print("\nMap values:");
foreach (int value in map1.values()) print(value, " ");
print("\nMap values union:");
set<int> mapValues = system.set(map1.values()) | system.set(map2.values());
foreach (int value in mapValues) print(value, " ");
print("");
