int LoopSum(int outer, int inner)
{
    int last = 0;
    for (int o in 0, outer, 1)
    {
        int bias = last & 1;
        int sum = 0;
        for (int i in 0, inner, 1)
            sum += i + bias;
        last = sum;
    }
    return last;
}

print(LoopSum(2, 6));
