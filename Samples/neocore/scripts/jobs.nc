import jobs;

bool RunJobs()
{
    JobPool pool = jobs.open("jobs_worker", 2);
    array<float> bias = array<float>(1);
    bias[0] = 10.0;
    pool.broadcast("SetBias", bias);

    list<array<float>> inputs = [];
    for (int i in 0, 4)
    {
        array<float> item = array<float>(1);
        item[0] = float(i + 1);
        inputs.append(item);
    }

    list<JobResult> results = pool.run("Compute", inputs, 10);
    pool.close();
    if (results.len() != inputs.len()) return false;
    for (int i in 0, results.len())
    {
        JobResult r = results[i];
        if (!r.ok) { print(r.error); return false; }
        print("input " .. inputs[i][0] .. " -> " .. r.floats[0]);
        if (r.floats[0] != inputs[i][0] + 10.0 || r.ints[0] != i + 1) return false;
    }
    return true;
}

bool passed = RunJobs();
print(passed);
export bool CheckPassed() { return passed; }
