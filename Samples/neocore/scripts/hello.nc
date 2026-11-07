﻿int ArrayRW(int size, int reps)
{
    list<int> a = [];
    a.resize(size);
    int last = 0;
    for (int r in 0, reps, 1)
    {
        int bias = last & 1;           // 직전 결과 의존
        for (int i in 0, size, 1)
            a[i] = (i + bias) & 255;
        int sum = 0;
        for (int i in 0, size, 1)
            sum += a[i];
        last = sum;
    }
    return last;
}

print(ArrayRW(200000, 20));
