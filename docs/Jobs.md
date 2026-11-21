# Parallel script jobs

Use `import jobs;` to run CPU work on background threads in an embedded application or the standalone console. Run the [sample](../Samples/neocore/scripts/jobs.nc) with `neocore --run jobs`. Its [worker module](../Lib/jobs_worker.nc) adds a bias to each input and returns the results in submission order. The regression test blocks inside two worker exports until both threads are present.

## Run a batch

```csharp
import jobs;

JobPool pool = jobs.open("jobs_worker", 4);
array<float> a = array<float>(1);
array<float> b = array<float>(1);
a[0] = 10.0;
b[0] = 20.0;
list<array<float>> inputs = [a, b];
list<JobResult> results = pool.run("Compute", inputs);
for (int i in 0, results.len())
{
    JobResult r = results[i];
    if (r.ok) print(r.floats[0]);
    else print(r.error);
}
pool.close();
```

Each input array is one job for the named export. Up to the configured number of workers execute concurrently. By default the calling thread waits for every item to finish. The returned list has exactly one result per input in its original position, even when a later item finishes first. An empty list returns immediately. A null input list or null item is a caller error detected before dispatch.

Each `JobResult` contains `bool ok`, `string error`, `array<float> floats` and `array<int> ints`. Success carries the worker outputs and an empty error string. A failed item carries the error and empty arrays. Other items still run. Batch results go directly to the caller. Asynchronous jobs posted on the same pool retain their IDs and remain available through `take` or `take32`.

Pass a third argument to limit the wait for the whole batch, e.g. `pool.run("Compute", inputs, 10)` for ten seconds or `0.25` for a quarter second. Omitting it or passing `-1` waits indefinitely. Zero returns a timeout result for every input without scheduling work. Other negative values, NaN and infinity are errors.

The deadline starts before the inputs are copied and includes queue time. Results published before the deadline keep their values or their original errors. Each unfinished input has `ok == false`, `error == "job timed out"` and empty output arrays. Queued items in that batch are removed. Active items are signalled to stop and their worker VMs are discarded before reuse. The caller receives the results without waiting for worker cleanup. A blocking native call can occupy a worker until it returns. The pool stays open and independent submissions keep running. Copying large inputs or building the result list can add time to the call.

Use `run` when the next step needs the batch results. All outputs remain in memory until the batch returns. Use `post` and `take` when a frame can use partial results. For large read-only data reused across jobs, `broadcast` can initialize each worker once. Individual inputs can then contain a range or an ID.

## Script interface

| Call | Behavior |
| --- | --- |
| `jobs.open(string module, int maxWorkers = 0) -> JobPool` | Resolve an import name using the entry program's module loader. Compile the worker and its imports into one image per pool. Zero uses the executor's concurrency. Positive limits are capped at that concurrency. |
| `pool.workers() -> int` | Return the pool's concurrency limit. |
| `pool.broadcast(string name, array<float> input)` | Set an initializer for subsequent submissions. Each worker applies it before processing its next applicable job. Broadcasting the same name replaces the old initializer for future submissions. |
| `pool.run(string name, list<array<float>> inputs, float timeoutSeconds = -1.0) -> list<JobResult>` | Run a synchronous batch and return regular-width results in input order. |
| `pool.post(string name, array<float> input) -> int` | Copy the input, enqueue a job, and return its positive ID. |
| `pool.post32(string name, array<float> input) -> int` | Enqueue a job with compact integer output. |
| `pool.take(array<float> floats, array<int> ints, array<int> info) -> int` | Poll for the oldest completed regular job. |
| `pool.take32(array<float> floats, array32<int> ints, array<int> info) -> int` | Poll for compact results using the same protocol. |
| `pool.error() -> string` | Read the error from the last consumed result. Consuming a successful result clears it. |
| `pool.pending() -> int` | Count queued jobs, jobs currently executing and completed results awaiting consumption. |
| `pool.clear()` | Discard queued jobs and results. Already executing jobs finish and their results are discarded. The setup and worker globals persist. |
| `pool.close()` | Discard queued work and unread results, signal running workers to stop, and wait for their VMs to be released. Closing again is safe. Resetting or destroying the owning instance also closes its pools. |

After `post` or `post32`, the owner can call `close()` while workers are busy. Worker VMs check for closure at script entry and periodically during execution, including global initialization, broadcast initializers and script callbacks. Even an infinite script loop can be stopped. A worker inside a native operation can only observe the request when execution returns to script. `close()` waits through that operation and cleanup. With `run`, the calling script can only issue its next command after the batch returns.

An initializer exports `void Setup(array<float> input)`. A regular worker exports `void Work(array<float> input, array<float> floats, array<int> ints)`. For `post32`, declare the third parameter as `array32<int>`. The worker sizes and fills the two empty output arrays. The caller may immediately change or reuse its input after posting. Results may arrive in any job-ID order.

Use a separate `array<int>(3)` for `info`. A poll returns zero when no result of that width is ready. Otherwise `info[0]` is 1 for success or 0 for failure, and `info[1]` and `info[2]` hold the required output lengths. A negative ID means the result is still queued. Resize the output arrays and retry. A positive ID consumes the result. On failure read `pool.error()` before consuming another result. Check both the ID and the success flag.

## Embedding and migration

1. Set `CompileOptions::moduleLoader` when compiling or loading the entry program. The callback and its captures must remain usable during later calls to `jobs.open`. It resolves the worker and imports on the calling thread. Each worker lazily loads the image in a separate runtime and runs its own global initialization. Opening the same module in a second pool compiles a second image.
2. For headless use, the default executor shares a lazily created team of OS threads across pools. To reuse an engine scheduler, pass a shared `IJobExecutor` via `CompileOptions::jobExecutor` at `Runtime::Create`. Its `Concurrency()` returns a positive limit. Its `Post` enqueues the callback on a background thread and returns immediately. If submission fails, throw before accepting the callback. Keep the scheduler alive through teardown.
3. Make worker exports self-contained. Workers receive the caller's compile-time defines and numeric data. Host bindings and a nested jobs loader are unavailable. Each worker owns its globals, including any cached initializer data. A failed job discards that worker's VM. Jobs must finish synchronously. If a process log hook is installed, it can be invoked concurrently from worker scripts.
4. When migrating from a host job service, replace the integer pool ID with a `JobPool` and call its methods on that handle. Keep the existing host service during migration. Before removing it, exercise both output widths, buffer resizing, initializer updates, failure reporting and scene teardown with jobs in flight. An engine scheduler used for other background work can remain the executor.
