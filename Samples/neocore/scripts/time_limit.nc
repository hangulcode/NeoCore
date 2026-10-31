export int TimeTest()
{
    int sum = 0;
    for (int i in 0, 5)
    {
        sum += i;
        sleep(1);
    }
    return sum;
}
