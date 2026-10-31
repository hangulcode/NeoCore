export void Time9(int n)
{
    for (int i in 1, 10)
        print(tostring(n) .. " x " .. tostring(i) .. " = " .. tostring(n * i));
}

for (int n in 1, 10) Time9(n);
