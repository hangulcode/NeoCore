int Sum1(int a, int b) { return a + b; }
export int Sum(int a, int b)
{
    int s = Sum1(a, b);
    int m = Host.mul(a, b);
    print(s, m);
    return s + m;
}
