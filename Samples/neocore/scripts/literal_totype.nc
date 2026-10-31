void Check(bool valid)
{
    if (!valid) { int zero = 0; print(1 / zero); }
}

list<int> one = [toint(1.9)];
Check(one.len() == 1 && one[0] == 1);
list<float> widened = [toint(2.7), 0.0];
Check(widened.len() == 2 && widened[0] == 2.0);
list<int> integers = [toint(3.9), "abc".len()];
list<float> floats = [tofloat(4)];
list<string> strings = [tostring(5)];
Check(integers[0] == 3 && integers[1] == 3 && floats[0] == 4.0 && strings[0] == "5");
list<int> first = [toint(1.5), 9, 8];
list<int> middle = [9, toint(1.5), 8];
list<int> last = [9, 8, toint(1.5)];
Check(first[0] == 1 && middle[1] == 1 && last[2] == 1);

list<float> InFunction(float value, Vector3 vector)
{
    return [toint(value), 0.0, vector.x, vector.y, vector.z];
}
list<float> local = InFunction(6.7, Vector3(1, 2, 3));
Check(local.len() == 5 && local[0] == 6.0 && local[2] == 1.0 && local[4] == 3.0);
list<int> Twice(float value) { return [toint(value), toint(value + 1.0)]; }
list<int> pair = Twice(1.2);
Check(pair.len() == 2 && pair[0] == 1 && pair[1] == 2);
list<list<int>> nested = [[toint(1.5)], [toint(2.5), toint(3.5)]];
Check(nested.len() == 2 && nested[0].len() == 1 && nested[1][1] == 3);
map<string, float> values = {"a": toint(9.9), "b": tofloat(2)};
map<string, int> keys = {tostring(7): 1};
Check(values.get("a", 0.0) == 9.0 && values.get("b", 0.0) == 2.0 && keys.get("7", 0) == 1);
list<list<int>> holder = [];
holder.append([toint(4.4), 5]);
Check(holder[0][0] == 4 && holder[0].len() == 2);
map<string, list<int>> mapList = {"values": [1, 2]};
list<int> found = mapList.get("values", []);
Check(found.len() == 2 && found[1] == 2);
print("literal_totype: typed conversion and nested literal checks passed");
