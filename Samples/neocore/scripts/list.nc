string FormatInts(list<int> values)
{
    string text = "[";
    for (int i in 0, values.len())
    {
        if (i != 0) text = text .. ", ";
        text = text .. tostring(values[i]);
    }
    return text .. "]";
}

list<int> values = [1, 2, 3];
values.append(4);
values.append(0, 0);
values.resize(3);
print("list : " .. FormatInts(values));
print("len : " .. tostring(values.len()));
if (values.len() != 3 || values[0] != 0 || values[2] != 2) { int zero = 0; print(1 / zero); }
