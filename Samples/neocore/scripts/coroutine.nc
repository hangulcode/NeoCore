import coroutine;

int completedSteps = 0;
void Worker(int start)
{
    for (int i in start, 5)
    {
        print("Coroutine " .. tostring(i));
        completedSteps += 1;
        yield;
    }
}

export void Test()
{
    coroutine<fn(int)->void> task = coroutine.create(Worker);
    for (int i in 0, 10)
    {
        string state = coroutine.status(task);
        if (state == "suspended") coroutine.resume(task, 2);
        print("Main " .. state .. tostring(i));
    }
    if (completedSteps != 3) { int zero = 0; print(1 / zero); }
    coroutine.close(task);
}
Test();
