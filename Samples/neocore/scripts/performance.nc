import math;
import system;

float CalculateSum(int n)
{
    float sum = 0.0;
    for (int i in 0, n) sum += i;
    return sum;
}

float CalculateMath(int n)
{
    float sum = 0.0;
    for (int i in 0, n) sum += math.sqrt(i);
    return sum;
}

bool IsPrime(int number)
{
    if (number < 2) return false;
    int end = int(math.sqrt(number)) + 1;
    for (int divisor in 2, end)
        if (number % divisor == 0) return false;
    return true;
}

int PrimeCount(int n)
{
    int count = 0;
    for (int i in 1, n) if (IsPrime(i)) count += 1;
    return count;
}

int Fibonacci(int n)
{
    if (n <= 1) return n;
    return Fibonacci(n - 1) + Fibonacci(n - 2);
}

print("Start ...");
float started = system.clock();
print("Loop Sum :" .. tostring(CalculateSum(100000001)));
print("Time :" .. tostring(system.clock() - started));
started = system.clock();
print("Math :" .. tostring(CalculateMath(100000001)));
print("Time :" .. tostring(system.clock() - started));
started = system.clock();
print("PrimeCount :" .. tostring(PrimeCount(5000001)));
print("Time :" .. tostring(system.clock() - started));
started = system.clock();
print("fibonacci :" .. tostring(Fibonacci(40)));
print("Time :" .. tostring(system.clock() - started));
