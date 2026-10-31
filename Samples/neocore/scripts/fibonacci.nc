int Fib(int n) {
    if (n < 2) return n;
    return Fib(n - 1) + Fib(n - 2);
}

void ShowResult(int n) {
    int result = Fib(n);
    print(result);
}

ShowResult(10);
